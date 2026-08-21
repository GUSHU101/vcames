package io.github.gushu101.vcames;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.BufferedOutputStream;
import java.io.Closeable;
import java.io.DataOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

final class FramePushClient implements Closeable {
    private static final String SOCKET_NAME = "vcamesd_frames";
    private static final int MAX_FRAME_BYTES = 16 * 1024 * 1024;

    private final LocalSocket socket;
    private final DataOutputStream output;

    FramePushClient() throws IOException {
        socket = new LocalSocket();
        socket.connect(new LocalSocketAddress(
                SOCKET_NAME,
                LocalSocketAddress.Namespace.ABSTRACT));
        output = new DataOutputStream(
                new BufferedOutputStream(socket.getOutputStream(), 64 * 1024));
        output.write("VCF2".getBytes(StandardCharsets.US_ASCII));
        output.flush();
    }

    void sendNv21(
            byte[] nv21,
            int width,
            int height,
            long presentationTimeNs) throws IOException {
        long expected = (long) width * height * 3L / 2L;
        if (width <= 0 || height <= 0 || (width & 1) != 0 || (height & 1) != 0
                || nv21.length != expected || nv21.length > MAX_FRAME_BYTES) {
            throw new IOException("NV21 frame metadata or payload is invalid");
        }
        output.writeInt(2); // VCF2 wire format: NV21
        output.writeInt(width);
        output.writeInt(height);
        output.writeInt(width); // tightly packed Y stride
        output.writeInt(width); // tightly packed interleaved VU stride
        output.writeInt(nv21.length);
        output.writeLong(presentationTimeNs);
        output.write(nv21);
        output.flush();
    }

    @Override
    public void close() throws IOException {
        try {
            output.close();
        } finally {
            socket.close();
        }
    }
}
