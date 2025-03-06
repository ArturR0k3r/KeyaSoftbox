#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN 4        // Pin connected to RGB LED strip
#define NUM_LEDS 30      // Adjust based on your LED strip

const char *ssid = "Softbox-LED";
const char *password = "12345678";

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
AsyncWebServer server(80);

void setColor(int r, int g, int b) {
    for (int i = 0; i < NUM_LEDS; i++) {
        strip.setPixelColor(i, strip.Color(r, g, b));
    }
    strip.show();
}

void setup() {
    Serial.begin(115200);
    WiFi.softAP(ssid, password);
    
    strip.begin();
    strip.show(); // Initialize all LEDs off

    server.on("/color", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("r") && request->hasParam("g") && request->hasParam("b")) {
            int r = request->getParam("r")->value().toInt();
            int g = request->getParam("g")->value().toInt();
            int b = request->getParam("b")->value().toInt();
            setColor(r, g, b);
            request->send(200, "text/plain", "Color updated");
        } else {
            request->send(400, "text/plain", "Missing parameters");
        }
    });
    
    server.begin();
}

void loop() {
    // Nothing needed here, everything handled by web server
}
