import java.io.*;
import java.net.Socket;
import java.util.*;

//bonus stuff:
//timer loop with a configurable polling rate, should be more efficient
//persistence
public class VacationResponder {

    //config
    private static final String POP3_HOST = "localhost";
    private static final int POP3_PORT = 110;

    private static final String SMTP_HOST = "localhost";
    private static final int SMTP_PORT = 1025;

    private static final String USERNAME = "me@localhost";
    private static final String PASSWORD = "password";
    private static final String MY_EMAIL = "me@localhost";

    private static final int POLL_INTERVAL_SECONDS = 30;

    //Persistence file, only reply to an address once
    private static final String REPLIED_FILE = "replied.txt";

    private static Set<String> repliedSenders = new HashSet<>();

    public static void main(String[] args) {
        loadReplied();

        while (true) {
            try {
                System.out.println("\n--- Checking mailbox ---");
                checkMailbox();
                saveReplied();
                Thread.sleep(POLL_INTERVAL_SECONDS * 1000);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    private static void checkMailbox() throws Exception {
        Socket socket = new Socket(POP3_HOST, POP3_PORT);
        BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        BufferedWriter out = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));

        readLine(in);

        send(out, "USER " + USERNAME);
        readLine(in);

        send(out, "PASS " + PASSWORD);
        readLine(in);

        send(out, "STAT");
        String stat = readLine(in);

        int messageCount = Integer.parseInt(stat.split(" ")[1]);

        for (int i = 1; i <= messageCount; i++) {
            send(out, "RETR " + i);
            List<String> message = readMultiline(in);
            processMessage(message);
        }

        send(out, "QUIT");
        readLine(in);
        socket.close();
    }

    private static void processMessage(List<String> lines) throws Exception {
        String from = null;
        String subject = null;

        boolean isBulk = false;
        boolean isAuto = false;

        for (String line : lines) {
            String lower = line.toLowerCase();

            if (lower.startsWith("from:")) {
                from = extractEmail(line);
            }

            if (lower.startsWith("subject:")) {
                subject = line.substring(8).trim();
            }

            //MAIL FILTERING
            if (lower.startsWith("precedence:") && (lower.contains("bulk") || lower.contains("list"))) {
                isBulk = true;
            }

            if (lower.startsWith("list-id:") || lower.startsWith("list-unsubscribe:")) {
                isBulk = true;
            }

            if (lower.startsWith("auto-submitted:")) {
                isAuto = true;
            }

            if (lower.startsWith("x-autorespond") || lower.startsWith("x-auto-response-suppress")) {
                isAuto = true;
            }
        }

        if (from == null || subject == null) return;

        if (!subject.equalsIgnoreCase("prac7")) return;

        if (repliedSenders.contains(from)) return;

        if (isBulk || isAuto) return;

        sendAutoReply(from);
        repliedSenders.add(from);
    }

    private static void sendAutoReply(String recipient) throws Exception {
        Socket socket = new Socket(SMTP_HOST, SMTP_PORT);
        BufferedReader in = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        BufferedWriter out = new BufferedWriter(new OutputStreamWriter(socket.getOutputStream()));

        readLine(in);

        send(out, "HELO localhost");
        readLine(in);

        send(out, "MAIL FROM:<" + MY_EMAIL + ">");
        readLine(in);

        send(out, "RCPT TO:<" + recipient + ">");
        readLine(in);

        send(out, "DATA");
        readLine(in);

        send(out, "Subject: Vacation Auto-Reply");
        send(out, "From: " + MY_EMAIL);
        send(out, "To: " + recipient);
        send(out, "");
        send(out, "I am on vacation leave me alone.");
        send(out, ".");
        readLine(in);

        send(out, "QUIT");
        readLine(in);

        socket.close();

        System.out.println("Replied to: " + recipient);
    }

    private static String extractEmail(String line) {
        int start = line.indexOf("<");
        int end = line.indexOf(">");

        if (start != -1 && end != -1) {
            return line.substring(start + 1, end);
        }

        return line.substring(5).trim();
    }

    private static void send(BufferedWriter out, String cmd) throws IOException {
        out.write(cmd + "\r\n");
        out.flush();
        System.out.println("CLIENT: " + cmd);
    }

    private static String readLine(BufferedReader in) throws IOException {
        String line = in.readLine();
        System.out.println("SERVER: " + line);
        return line;
    }

    private static List<String> readMultiline(BufferedReader in) throws IOException {
        List<String> lines = new ArrayList<>();
        String line;

        while ((line = in.readLine()) != null) {
            if (line.equals(".")) break;
            lines.add(line);
        }

        return lines;
    }

    //persistence
    private static void loadReplied() {
        try (BufferedReader br = new BufferedReader(new FileReader(REPLIED_FILE))) {
            String line;
            while ((line = br.readLine()) != null) {
                repliedSenders.add(line.trim());
            }
        } catch (IOException ignored) {}
    }

    private static void saveReplied() {
        try (BufferedWriter bw = new BufferedWriter(new FileWriter(REPLIED_FILE))) {
            for (String s : repliedSenders) {
                bw.write(s);
                bw.newLine();
            }
        } catch (IOException ignored) {}
    }
}