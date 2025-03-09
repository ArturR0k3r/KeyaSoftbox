#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>  // Use the standard ESP8266WebServer library
#include <Adafruit_NeoPixel.h>
#include <ESP8266mDNS.h>
#include <ElegantOTA.h>

#define LED_PIN    0   // Pin connected to the Data In of the matrix (GPIO 0 or D3)
#define NUM_LEDS   64  // 8x8 matrix = 64 LEDs

const char* ssid = "Softbox-LED";
const char* password = "12345678";

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);  // WS2812/WS2813 configuration
ESP8266WebServer server(80);  // Create the web server on port 80

// Function to set the color of the LEDs
void setColor(int r, int g, int b) {
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(r, g, b));  // Set color for each pixel
    }
    strip.show();  // Update the LED strip
}

// HTML page served by the ESP8266 (User Interface)
String htmlPage = "<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>LED Control</title>"
"<style>"
"body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }"
"h1 { color: #4CAF50; }"
"input { margin: 5px; }"
"button { background-color: #4CAF50; color: white; padding: 10px 20px; border: none; cursor: pointer; }"
"button:hover { background-color: #45a049; }"
"</style>"
"</head>"
"<body>"
"<h1>Control LED Color</h1>"
"<p>Pick a color using the color picker and click 'Set Color' to change the LED color:</p>"
"<form action='/color' method='get'>"
"  <input type='color' name='color' value='#ff0000' required><br>"  // Default color is red
"  <button type='submit'>Set Color</button>"
"</form>"
"</body>"
"</html>";

void handleRoot() {
    // Send the HTML page to the client
    server.send(200, "text/html", htmlPage);
}

void handleColor() {
    if (server.hasArg("color")) {
        String colorHex = server.arg("color");
        long color = strtol(colorHex.c_str() + 1, NULL, 16);  // Convert hex color to long
        int r = (color >> 16) & 0xFF;  // Extract Red
        int g = (color >> 8) & 0xFF;   // Extract Green
        int b = color & 0xFF;          // Extract Blue
        setColor(r, g, b);  // Set the LED color
        server.send(200, "text/plain", "Color updated to " + colorHex);
    } else {
        server.send(400, "text/plain", "Missing parameters");
    }
}

void setup() {
    Serial.begin(115200);
    WiFi.softAP(ssid, password);
    Serial.println("Access Point Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    delay(1000);  // Wait to ensure network is up

    strip.begin();
    strip.show();  // Initialize all LEDs off

    // Web endpoint for serving the HTML page
    server.on("/", HTTP_GET, handleRoot);

    // Web endpoint for controlling LED color
    server.on("/color", HTTP_GET, handleColor);

    // Start the server
    server.begin();
    Serial.println("HTTP server started");

    // Initialize mDNS
    if (MDNS.begin("esp8266")) {
        Serial.println("mDNS responder started");
    } else {
        Serial.println("Error setting up mDNS responder!");
    }
}

void loop() {
    server.handleClient();  // Handle client requests
    MDNS.update();
}
