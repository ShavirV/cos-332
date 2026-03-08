package prac3;

import java.io.*;
import java.net.*;
import java.time.*;
import java.time.format.*;
import java.util.*;

public class WorldClockServer{

    private static final int PORT = 55555;

    //store capital city/continent pairs
    private static final Map<String, String> cities = new LinkedHashMap<>();

        static {
        cities.put("London", "Europe/London");
        cities.put("NewYork", "America/New_York");
        cities.put("Tokyo", "Asia/Tokyo");
        cities.put("Sydney", "Australia/Sydney");
        cities.put("Dubai", "Asia/Dubai");
        cities.put("Paris", "Europe/Paris");
    }

    public static void main(String[] args) throws Exception{

        ServerSocket server = new ServerSocket(PORT);
        System.out.println("Server running on port " + PORT);

        while (true){
            Socket client = server.accept();
            handleClient(client);
        }
    }

    public static void handleClient(Socket client){
        try{
            BufferedReader in = new BufferedReader(new InputStreamReader((client.getInputStream())));

            OutputStream out = client.getOutputStream();
            PrintWriter writer = new PrintWriter(out);

            String requestLine = in.readLine();

            if (requestLine == null){
                client.close();
                return;
            }

            //example request: GET /?city=Tokyo HTTP/1.1
            String city = null;

            if (requestLine.contains("?city=")){
                int start = requestLine.indexOf("?city=") + 6;
                int end = requestLine.indexOf(" ", start);
                city = requestLine.substring(start, end);
            }

            String html = buildPage(city);

            writer.println("HTTP/1.1 200 OK");
            writer.println("Content-Type: text/html");
            writer.println("Connection: close");
            writer.println();
            writer.println(html);

            writer.flush();
            client.close();

        } catch (Exception e){
            e.printStackTrace();
        }
    }

    private static String buildPage(String city){
        StringBuilder html = new StringBuilder();

        DateTimeFormatter formatter =
                DateTimeFormatter.ofPattern("HH:mm:ss");

        ZonedDateTime saTime =
                ZonedDateTime.now(ZoneId.of("Africa/Johannesburg"));

        html.append("<html>");
        html.append("<head>");
        html.append("<title>World Clock</title>");
        html.append("<meta http-equiv='refresh' content='1'>");
        html.append("</head>");
        html.append("<body>");

        html.append("<h1>World Clock</h1>");

        html.append("<h2>South Africa Time: ")
                .append(saTime.format(formatter))
                .append("</h2>");

        if (city != null && cities.containsKey(city)) {

            ZonedDateTime cityTime =
                    ZonedDateTime.now(ZoneId.of(cities.get(city)));

            html.append("<h2>")
                    .append(city)
                    .append(" Time: ")
                    .append(cityTime.format(formatter))
                    .append("</h2>");
        }

        html.append("<h3>Select a city:</h3>");

        for (String c : cities.keySet()) {
            html.append("<a href='/?city=")
                    .append(c)
                    .append("'>")
                    .append(c)
                    .append("</a><br>");
        }

        html.append("</body>");
        html.append("</html>");

        return html.toString();
    }
}