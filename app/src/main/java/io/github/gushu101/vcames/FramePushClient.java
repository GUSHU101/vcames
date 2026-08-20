package io.github.gushu101.vcames;

import android.net.LocalSocket;
import android.net.LocalSocketAddress;

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
        output = new DataOutputStream(socket.getOutputStream());
        output.write("VCF1".getBytes(StandardCharsets.US_ASCII));
        output.flush();
    }

    void send(byte[] jpeg) throws IOException {
        if (jpeg.length < 4 || jpeg.length > MAX_FRAME_BYTES) {
            throw new IOException("JPEG frame is empty or too large");
        }
        output.writeInt(jpeg.length);
        output.write(jpeg);
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
