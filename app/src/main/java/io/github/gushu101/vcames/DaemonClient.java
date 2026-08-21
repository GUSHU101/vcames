package io.github.gushu101.vcames;

import android.content.Context;
import android.net.LocalSocket;
import android.net.LocalSocketAddress;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

final class DaemonClient {
    private static final String SOCKET_NAME = "vcamesd";
    private static final int MAX_RESPONSE_BYTES = 64 * 1024;

    private DaemonClient() {}

    static String start(Context context, VCamConfig config) throws IOException {
        return request(config.toStartCommand());
    }

    static String stop() throws IOException {
        return request("STOP\n.\n");
    }

    static String status() throws IOException {
        return request("STATUS\n.\n");
    }

    private static String request(String command) throws IOException {
        try (LocalSocket socket = new LocalSocket()) {
            socket.connect(
                    new LocalSocketAddress(SOCKET_NAME, LocalSocketAddress.Namespace.ABSTRACT));
            // START waits until the black/first frame has actually reached
            // /dev/video100, so allow the daemon's bounded five-second gate.
            socket.setSoTimeout(8000);
            socket.getOutputStream().write(command.getBytes(StandardCharsets.UTF_8));
            socket.getOutputStream().flush();
            socket.shutdownOutput();

            ByteArrayOutputStream response = new ByteArrayOutputStream();
            byte[] chunk = new byte[2048];
            int count;
            while ((count = socket.getInputStream().read(chunk)) >= 0) {
                if (response.size() + count > MAX_RESPONSE_BYTES) {
                    throw new IOException("vcamesd response is too large");
                }
                response.write(chunk, 0, count);
            }
            String result = new String(response.toByteArray(), StandardCharsets.UTF_8).trim();
            if (result.contains("\"ok\":false")) {
                throw new IOException("vcamesd rejected the request: " + result);
            }
            return result;
        }
    }
}
