package prac3;

import java.io.*;
import java.net.*;
import java.time.*;
import java.time.format.*;
import java.util.*;

public class WorldClockServer{

    private static final int PORT = 55555;

    //store capital city/zoneID pairs
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
        System.out.println("http://127.0.0.1:55555/");

        while (true){
            Socket client = server.accept();
            new Thread (() -> handleClient(client)).start();
        }
    }

    public static void handleClient(Socket client){

        String status = "200"; //start off with a valid request, update as needed
        try{
            BufferedReader in = new BufferedReader(new InputStreamReader((client.getInputStream())));

            OutputStream out = client.getOutputStream();
            PrintWriter writer = new PrintWriter(out);

            String requestLine = in.readLine();

            if (requestLine == null){
                client.close();
                return;
            }

            //split requests more cleanly
            StringTokenizer tokenizer = new StringTokenizer(requestLine);

            //example request: GET /?city=Tokyo HTTP/1.1
            String method = tokenizer.nextToken();
            String path = tokenizer.nextToken();

            //wait for input
            while(in.readLine().length() != 0) {}

//bonus marks here :D

            //handle invalid request types, like POST
            if(!method.equals("GET") && !method.equals("HEAD")){
                status = "405"; //method not allowed

                sendResponse(writer, 
                    "405 Method Not Allowed",
                    "<h1> 405 Method Not Allowed</h1> <h2>Stop poking around👀</h2>",
                    method.equals("HEAD")
                    );
                log(client, requestLine, status);
                client.close();
                return;
            }

            //reroute "/" to the actual page
            if (path.equals("/")){
                status = "301"; //moved permanently 

                writer.println("HTTP/1.1 301 Moved Permanently");
                writer.println("Location: /clock");
                writer.println("connection: close");
                writer.flush();

                log(client, requestLine, status);
                client.close();
                return;
            }

            //handle regular requests
            if(path.startsWith("/clock")){
                String city = null;

                //get city name 
                if (path.contains("?city=")){
                    city = path.substring(path.indexOf("?city=")+6); //actual start of city name
                }

                //invalid city
                if (city != null && !cities.containsKey(city)){
                    status = "404";

                    sendResponse(writer,
                        "404 Not Found",
                        "<h1>404 - City not found. Maybe its not real</h1>",
                        method.equals("HEAD")
                    );

                    log(client, requestLine, status);
                    client.close();
                    return;
                }

                String html = buildPage(city);

                sendResponse(writer, "200 OK", html, method.equals("HEAD"));

                log(client, requestLine, status);
            }

            client.close();

        } catch (Exception e){
            e.printStackTrace();
        }
    }

     private static String buildPage(String city){

        DateTimeFormatter formatter =
                DateTimeFormatter.ofPattern("HH:mm:ss");

        ZonedDateTime saTime =
                ZonedDateTime.now(ZoneId.of("Africa/Johannesburg"));

        StringBuilder html = new StringBuilder();

        html.append("<html>");
        html.append("<head>");
        html.append("<title>World Cloc</title>");
        html.append("<meta http-equiv='refresh' content='1'>");
        html.append("</head>");
        html.append("<body>");

        html.append("<h1>World Cloc</h1>");

        html.append("<h2>South Africa Time: ")
                .append(saTime.format(formatter))
                .append("</h2>");

        if(city != null) {

            ZonedDateTime cityTime =
                    ZonedDateTime.now(ZoneId.of(cities.get(city)));

            html.append("<h2>")
                    .append(city)
                    .append(" Time: ")
                    .append(cityTime.format(formatter))
                    .append("</h2>");
        }

        html.append("<h3>Select a city:</h3>");

        for(String c : cities.keySet()) {
            html.append("<a href='/clock?city=")
                    .append(c)
                    .append("'>")
                    .append(c)
                    .append("</a><br>");
        }

        // Demo section to show HTTP features
        html.append("<hr>");
        html.append("<h3>HTTP Demo Links</h3>");

        html.append("<a href='/'>Test 301 Redirect</a><br>");
        html.append("<a href='/clock?city=Kottayam'>Test 404</a><br>");

        html.append("<p>405 test (Method Not Allowed):</p>");
        html.append("<form action='/clock' method='POST'>");
        html.append("<input type='submit' value='Trigger 405'>");
        html.append("</form>");

        html.append("</body></html>");

        return html.toString();
    }


    private static void sendResponse(PrintWriter out, String status,
                                     String body, boolean headOnly) {

        out.println("HTTP/1.1 " + status);
        out.println("Content-Type: text/html; charset=UTF-8");
        out.println("Content-Length: " + body.getBytes().length);
        out.println("Server: JavaWorldClock");
        out.println("Connection: close");
        out.println();

        if(!headOnly)
            out.println(body);

        out.flush();
    }

    private static void log(Socket client, String request, String status) {

        System.out.println(
            client.getInetAddress().getHostAddress()
            + " - \"" + request + "\" "
            + status
        );
    }

    
}