package io.github.gushu101.vcames;

import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.media.Image;

import java.io.IOException;
import java.nio.ByteBuffer;

final class Yuv420Converter {
    static {
        System.loadLibrary("vcames_yuv");
    }

    private byte[] output = new byte[0];
    private int outputWidth;
    private int outputHeight;

    byte[] convert(Image image) throws IOException {
        if (image.getFormat() != ImageFormat.YUV_420_888 || image.getPlanes().length < 3) {
            throw new IOException("Decoder did not produce YUV_420_888 images");
        }
        Rect crop = image.getCropRect();
        int cropLeft = crop.left & ~1;
        int cropTop = crop.top & ~1;
        int width = (crop.right - cropLeft) & ~1;
        int height = (crop.bottom - cropTop) & ~1;
        if (width <= 0 || height <= 0) {
            throw new IOException("Decoder produced an empty image");
        }
        long required = (long) width * height * 3L / 2L;
        if (required > Integer.MAX_VALUE) {
            throw new IOException("Decoder image is too large");
        }
        if (output.length != (int) required) {
            output = new byte[(int) required];
        }

        Image.Plane[] planes = image.getPlanes();
        ByteBuffer y = planes[0].getBuffer();
        ByteBuffer u = planes[1].getBuffer();
        ByteBuffer v = planes[2].getBuffer();
        if (!nativeConvert(
                y, y.limit(), y.position(), planes[0].getRowStride(), planes[0].getPixelStride(),
                u, u.limit(), u.position(), planes[1].getRowStride(), planes[1].getPixelStride(),
                v, v.limit(), v.position(), planes[2].getRowStride(), planes[2].getPixelStride(),
                cropLeft, cropTop, width, height, output)) {
            throw new IOException("Native YUV conversion rejected plane strides or buffers");
        }
        outputWidth = width;
        outputHeight = height;
        return output;
    }

    int width() {
        return outputWidth;
    }

    int height() {
        return outputHeight;
    }

    private static native boolean nativeConvert(
            ByteBuffer yBuffer,
            int yLimit,
            int yPosition,
            int yRowStride,
            int yPixelStride,
            ByteBuffer uBuffer,
            int uLimit,
            int uPosition,
            int uRowStride,
            int uPixelStride,
            ByteBuffer vBuffer,
            int vLimit,
            int vPosition,
            int vRowStride,
            int vPixelStride,
            int cropLeft,
            int cropTop,
            int width,
            int height,
            byte[] output);
}
