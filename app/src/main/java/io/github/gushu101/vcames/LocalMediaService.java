package io.github.gushu101.vcames;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.ImageFormat;
import android.media.Image;
import android.media.ImageReader;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.net.Uri;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.SystemClock;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

public final class LocalMediaService extends Service {
    private static final String TAG = "VCamES.Media";
    private static final String ACTION_START = "io.github.gushu101.vcames.START_LOCAL";
    private static final String ACTION_STOP = "io.github.gushu101.vcames.STOP_LOCAL";
    private static final String EXTRA_URI = "uri";
    private static final String CHANNEL_ID = "vcames_local_media";
    private static final int NOTIFICATION_ID = 41002;

    private final Object workerLock = new Object();
    private AtomicBoolean stopRequested = new AtomicBoolean(true);
    private Thread worker;

    public static void start(Context context, String uri) {
        Intent intent = new Intent(context, LocalMediaService.class)
                .setAction(ACTION_START)
                .putExtra(EXTRA_URI, uri);
        context.startForegroundService(intent);
    }

    public static void stop(Context context) {
        context.startService(new Intent(context, LocalMediaService.class).setAction(ACTION_STOP));
    }

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationManager manager = getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(
                CHANNEL_ID,
                "VCamES 本地视频",
                NotificationManager.IMPORTANCE_LOW));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopPlayback();
            stopForeground(STOP_FOREGROUND_REMOVE);
            stopSelf();
            return START_NOT_STICKY;
        }
        String uri = intent.getStringExtra(EXTRA_URI);
        if (!ACTION_START.equals(intent.getAction()) || uri == null || uri.isBlank()) {
            stopSelf();
            return START_NOT_STICKY;
        }
        startForeground(NOTIFICATION_ID, createNotification());
        startPlayback(uri);
        return START_REDELIVER_INTENT;
    }

    @Override
    public void onDestroy() {
        stopPlayback();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private Notification createNotification() {
        PendingIntent open = PendingIntent.getActivity(
                this,
                0,
                new Intent(this, MainActivity.class),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        PendingIntent stop = PendingIntent.getService(
                this,
                1,
                new Intent(this, LocalMediaService.class).setAction(ACTION_STOP),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.presence_video_online)
                .setContentTitle("VCamES 本地视频运行中")
                .setContentText("正在向系统虚拟相机循环推送视频")
                .setContentIntent(open)
                .setOngoing(true)
                .addAction(new Notification.Action.Builder(
                        android.R.drawable.ic_media_pause,
                        "停止",
                        stop).build())
                .build();
    }

    private void startPlayback(String uri) {
        stopPlayback();
        AtomicBoolean nextStop = new AtomicBoolean(false);
        Thread nextWorker = new Thread(
                () -> playbackLoop(Uri.parse(uri), nextStop),
                "vcames-local-decoder");
        synchronized (workerLock) {
            stopRequested = nextStop;
            worker = nextWorker;
        }
        nextWorker.start();
    }

    private void stopPlayback() {
        final Thread oldWorker;
        synchronized (workerLock) {
            stopRequested.set(true);
            oldWorker = worker;
            worker = null;
        }
        if (oldWorker != null && oldWorker != Thread.currentThread()) {
            oldWorker.interrupt();
            try {
                oldWorker.join(2500);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void playbackLoop(Uri uri, AtomicBoolean stop) {
        VCamConfig config = VCamConfig.load(this);
        while (!stop.get()) {
            try (FramePushClient sender = new FramePushClient()) {
                decodeOnce(uri, config.fps, sender, stop);
            } catch (IOException | RuntimeException e) {
                Log.e(TAG, "Local video pipeline failed", e);
                if (!sleepInterruptibly(1000, stop)) {
                    break;
                }
            }
        }
    }

    private void decodeOnce(
            Uri uri,
            int maximumFps,
            FramePushClient sender,
            AtomicBoolean stop) throws IOException {
        try (android.content.res.AssetFileDescriptor asset =
                     getContentResolver().openAssetFileDescriptor(uri, "r")) {
            if (asset == null) {
                throw new IOException("Unable to open the selected video");
            }
            MediaExtractor extractor = new MediaExtractor();
            HandlerThread imageThread = null;
            ImageReader reader = null;
            MediaCodec decoder = null;
            try {
                FileDescriptor descriptor = asset.getFileDescriptor();
                if (asset.getLength() >= 0) {
                    extractor.setDataSource(
                            descriptor,
                            asset.getStartOffset(),
                            asset.getLength());
                } else {
                    extractor.setDataSource(descriptor);
                }
                int trackIndex = findVideoTrack(extractor);
                if (trackIndex < 0) {
                    throw new IOException("Selected file has no video track");
                }
                extractor.selectTrack(trackIndex);
                MediaFormat format = extractor.getTrackFormat(trackIndex);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime == null) {
                    throw new IOException("Video track has no MIME type");
                }
                int width = format.getInteger(MediaFormat.KEY_WIDTH);
                int height = format.getInteger(MediaFormat.KEY_HEIGHT);
                if (width < 2 || height < 2 || width > 3840 || height > 2160) {
                    throw new IOException("Video dimensions are outside the supported range");
                }
                width &= ~1;
                height &= ~1;

                AtomicReference<IOException> frameError = new AtomicReference<>();
                imageThread = new HandlerThread("vcames-image-reader");
                imageThread.start();
                reader = ImageReader.newInstance(width, height, ImageFormat.YUV_420_888, 3);
                final long minimumFrameIntervalNs = 1_000_000_000L / maximumFps;
                final long[] lastSentNs = {0};
                final Yuv420Converter converter = new Yuv420Converter();
                reader.setOnImageAvailableListener(imageReader -> {
                    try (Image image = imageReader.acquireLatestImage()) {
                        if (image == null || stop.get() || frameError.get() != null) {
                            return;
                        }
                        long now = System.nanoTime();
                        if (lastSentNs[0] != 0 && now - lastSentNs[0] < minimumFrameIntervalNs) {
                            return;
                        }
                        byte[] frame = converter.convert(image);
                        sender.sendNv21(
                                frame,
                                converter.width(),
                                converter.height(),
                                now);
                        lastSentNs[0] = now;
                    } catch (IOException | RuntimeException e) {
                        frameError.compareAndSet(
                                null,
                                e instanceof IOException
                                        ? (IOException) e
                                        : new IOException("Image conversion failed", e));
                    }
                }, new Handler(imageThread.getLooper()));

                decoder = MediaCodec.createDecoderByType(mime);
                decoder.configure(format, reader.getSurface(), null, 0);
                decoder.start();
                runDecoder(extractor, decoder, frameError, stop);
                IOException asynchronousError = frameError.get();
                if (asynchronousError != null) {
                    throw asynchronousError;
                }
            } finally {
                if (decoder != null) {
                    try {
                        decoder.stop();
                    } catch (IllegalStateException ignored) {
                        // Decoder can already be stopped after a codec failure.
                    }
                    decoder.release();
                }
                if (reader != null) {
                    reader.close();
                }
                if (imageThread != null) {
                    imageThread.quitSafely();
                    try {
                        imageThread.join(1000);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                    }
                }
                extractor.release();
            }
        }
    }

    private static int findVideoTrack(MediaExtractor extractor) {
        for (int index = 0; index < extractor.getTrackCount(); ++index) {
            String mime = extractor.getTrackFormat(index).getString(MediaFormat.KEY_MIME);
            if (mime != null && mime.startsWith("video/")) {
                return index;
            }
        }
        return -1;
    }

    private static void runDecoder(
            MediaExtractor extractor,
            MediaCodec decoder,
            AtomicReference<IOException> frameError,
            AtomicBoolean stop) throws IOException {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        boolean inputEnded = false;
        boolean outputEnded = false;
        long firstPresentationUs = -1;
        long playbackStartNs = 0;
        while (!stop.get() && !outputEnded && frameError.get() == null) {
            if (!inputEnded) {
                int inputIndex = decoder.dequeueInputBuffer(10_000);
                if (inputIndex >= 0) {
                    ByteBuffer input = decoder.getInputBuffer(inputIndex);
                    if (input == null) {
                        throw new IOException("Decoder returned a null input buffer");
                    }
                    int sampleSize = extractor.readSampleData(input, 0);
                    if (sampleSize < 0) {
                        decoder.queueInputBuffer(
                                inputIndex,
                                0,
                                0,
                                0,
                                MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        inputEnded = true;
                    } else {
                        decoder.queueInputBuffer(
                                inputIndex,
                                0,
                                sampleSize,
                                extractor.getSampleTime(),
                                extractor.getSampleFlags());
                        extractor.advance();
                    }
                }
            }

            int outputIndex = decoder.dequeueOutputBuffer(info, 10_000);
            if (outputIndex >= 0) {
                boolean endOfStream = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                boolean codecConfig = (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0;
                // A decoder configured with an output Surface can report size == 0
                // even though the Surface contains a valid frame.
                if (!endOfStream && !codecConfig) {
                    if (firstPresentationUs < 0) {
                        firstPresentationUs = info.presentationTimeUs;
                        playbackStartNs = System.nanoTime();
                    }
                    long targetNs = playbackStartNs
                            + (info.presentationTimeUs - firstPresentationUs) * 1000L;
                    waitUntil(targetNs, stop);
                    decoder.releaseOutputBuffer(outputIndex, true);
                } else {
                    decoder.releaseOutputBuffer(outputIndex, false);
                }
                outputEnded = endOfStream;
            }
        }
        if (!stop.get() && frameError.get() == null) {
            SystemClock.sleep(100);
        }
    }

    private static void waitUntil(long targetNs, AtomicBoolean stop) {
        while (!stop.get()) {
            long remainingNs = targetNs - System.nanoTime();
            if (remainingNs <= 0) {
                return;
            }
            long sleepMs = Math.min(remainingNs / 1_000_000L, 20L);
            if (sleepMs <= 0) {
                Thread.yield();
            } else {
                SystemClock.sleep(sleepMs);
            }
        }
    }

    private static boolean sleepInterruptibly(long millis, AtomicBoolean stop) {
        long deadline = SystemClock.elapsedRealtime() + millis;
        while (!stop.get()) {
            long remaining = deadline - SystemClock.elapsedRealtime();
            if (remaining <= 0) {
                return true;
            }
            SystemClock.sleep(Math.min(remaining, 100));
        }
        return false;
    }

}
