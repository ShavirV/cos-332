/*
 * BirthdayReminderSMTP.java
 *
 *
 * If any events occur exactly 6 days from today,
 * sends reminder email via raw SMTP socket protocol.
 *
 * Example usage:
 * java BirthdayReminderSMTP events.txt smtp.example.com 25 sender@example.com recipient@example.com
 */

import java.io.*;
import java.net.Socket;
import java.time.LocalDate;
import java.util.*;

public class BirthdayReminderSMTP {

    public static void main(String[] args) {
        if (args.length != 5) {
            System.out.println("Usage:");
            System.out.println("java BirthdayReminderSMTP <eventsFile> <smtpServer> <port> <fromEmail> <toEmail>");
            return;
        }

        String eventsFile = args[0];
        String smtpServer = args[1];
        int port = Integer.parseInt(args[2]);
        String fromEmail = args[3];
        String toEmail = args[4];

        try {
            List<Event> reminders = findUpcomingEvents(eventsFile);

            if (reminders.isEmpty()) {
                System.out.println("No events exactly 6 days from today");
                return;
            }

            String emailBody = buildReminderMessage(reminders);

            SMTPClient smtp = new SMTPClient(smtpServer, port);
            smtp.sendMail(fromEmail, toEmail,
                    "Reminder: Upcoming Events in 6 Days. You are a terrible friend if you forgot.",
                    emailBody);

            System.out.println("Reminder email sent successfully.");

        } catch (Exception e) {
            e.printStackTrace();
        }
    }

    //event logic
    static List<Event> findUpcomingEvents(String filename) throws IOException {
        List<Event> matchingEvents = new ArrayList<>();
        BufferedReader br = new BufferedReader(new FileReader(filename));

        LocalDate today = LocalDate.now();
        LocalDate targetDate = today.plusDays(6);

        String line;
        while ((line = br.readLine()) != null) {
            line = line.trim();
            if (line.isEmpty()) continue;

            Event event = parseEvent(line);
            if (event != null) {
                if (event.day == targetDate.getDayOfMonth()
                        && event.month == targetDate.getMonthValue()) {
                    matchingEvents.add(event);
                }
            }
        }

        br.close();
        return matchingEvents;
    }

    static Event parseEvent(String line) {
        try {
            String[] parts = line.split(" ", 2);
            String[] dateParts = parts[0].split("/");

            int day = Integer.parseInt(dateParts[0]);
            int month = Integer.parseInt(dateParts[1]);
            String description = parts[1];

            return new Event(day, month, description);
        } catch (Exception e) {
            System.out.println("Skipping invalid line: " + line);
            return null;
        }
    }

    static String buildReminderMessage(List<Event> events) {
        StringBuilder sb = new StringBuilder();
        sb.append("The following events occur in exactly 6 days:\n\n");

        for (Event e : events) {
            sb.append(String.format("%02d/%02d - %s\n", e.day, e.month, e.description));
        }

        sb.append("\nThis is an automated reminder.");
        return sb.toString();
    }

    //wrapper for each line
    static class Event {
        int day;
        int month;
        String description;

        Event(int day, int month, String description) {
            this.day = day;
            this.month = month;
            this.description = description;
        }
    }

    static class SMTPClient {
        private String server;
        private int port;

        SMTPClient(String server, int port) {
            this.server = server;
            this.port = port;
        }

        public void sendMail(String from, String to, String subject, String body) throws IOException {
            Socket socket = new Socket(server, port);

            BufferedReader in = new BufferedReader(
                    new InputStreamReader(socket.getInputStream()));
            BufferedWriter out = new BufferedWriter(
                    new OutputStreamWriter(socket.getOutputStream()));

            readResponse(in);

            //HELO
            sendCommand(out, "HELO localhost");
            readResponse(in);

            //MAIL FROM
            sendCommand(out, "MAIL FROM:<" + from + ">");
            readResponse(in);

            //RCPT TO
            sendCommand(out, "RCPT TO:<" + to + ">");
            readResponse(in);

            //DATA
            sendCommand(out, "DATA");
            readResponse(in);

            //Email headers + body
            out.write("From: " + from + "\r\n");
            out.write("To: " + to + "\r\n");
            out.write("Subject: " + subject + "\r\n");
            out.write("\r\n");
            out.write(body + "\r\n");
            out.write(".\r\n");
            out.flush();

            readResponse(in);

            //QUIT
            sendCommand(out, "QUIT");
            readResponse(in);

            socket.close();
        }

        private void sendCommand(BufferedWriter out, String cmd) throws IOException {
            System.out.println("CLIENT: " + cmd);
            out.write(cmd + "\r\n");
            out.flush();
        }

        private void readResponse(BufferedReader in) throws IOException {
            String response = in.readLine();
            System.out.println("SERVER: " + response);

            if (response == null || response.length() < 3) {
                throw new IOException("Invalid SMTP response");
            }

            String code = response.substring(0, 3);
            int status = Integer.parseInt(code);

            if (status >= 400) {
                throw new IOException("SMTP Error: " + response);
            }
        }
    }
}