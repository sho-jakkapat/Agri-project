#define _USE_MATH_DEFINES
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <cmath>

// --- ⚠️ แก้ชื่อ WiFi ตรงนี้ครับ ⚠️ ---
const char* ssid = "";      // ใส่ชื่อ WiFi
const char* password = "";  // ใส่รหัสผ่าน
// ----------------------------------

// IP เครื่องพี่ (Server)
const String serverUrl = "http://172.20.10.4:5000/api/log";

#define LED_PIN 2
double prevLat = 0.0, prevLon = 0.0;
double accumulatedDistance = 0.0;
bool firstFix = true;

// สูตรคำนวณระยะทาง (Haversine Formula) - แก้ไขให้ถูกต้องแล้วครับ
double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    double R = 6371000; // รัศมีโลก (เมตร)
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a)); // เพิ่มบรรทัดนี้กลับมาแล้วครับ
    return R * c;
}

void sendDataToServer(double lat, double lon, double dist, String action) {
    if(WiFi.status() == WL_CONNECTED){
        HTTPClient http;
        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/json");

        // --- แก้ชื่อตัวแปรให้ตรงกับ Python เป๊ะๆ ---
        String jsonPayload = "{";
        jsonPayload += "\"latitude\":" + String(lat, 6) + ",";       // ต้องใช้คำเต็ม
        jsonPayload += "\"longitude\":" + String(lon, 6) + ",";      // ต้องใช้คำเต็ม
        jsonPayload += "\"gps_fix_status\": 1,";                     // อันนี้ต้องใส่ (Python บังคับ)
        jsonPayload += "\"distance\":" + String(dist, 2) + ",";
        jsonPayload += "\"action_status\":\"" + action + "\"";       // แก้ action -> action_status
        jsonPayload += "}";
        // ----------------------------------------

        Serial.print("Sending to Server... ");
        int httpResponseCode = http.POST(jsonPayload);

        if(httpResponseCode > 0){
            Serial.println("OK! Code: " + String(httpResponseCode));
            // ถ้าอยากรู้ว่า Server ตอบว่าอะไร (เผื่อ Error อีก) ให้เปิดบรรทัดล่างนี้ครับ
            // Serial.println(http.getString()); 
        } else {
            Serial.println("Error: " + String(httpResponseCode));
        }
        http.end();
    } else {
        Serial.println("WiFi Disconnected!");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); 

    delay(1000);
    Serial.println("\n--- ESP32 Online Mode ---");
    
    // เชื่อมต่อ WiFi
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    Serial.print("My IP: ");
    Serial.println(WiFi.localIP());
    
    Serial.println("Ready! Type JSON to simulate GPS.");
}

void loop() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        if (input.length() > 0) {
            Serial.print("CHECK INPUT: "); Serial.println(input);

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, input);

            if (!error) {
                double currentLat = doc["lat"];
                double currentLon = doc["lon"];
                String actionStatus = "FLYING";

                if (firstFix) {
                    prevLat = currentLat;
                    prevLon = currentLon;
                    firstFix = false;
                    Serial.println(">>> First Fix Set");
                    // ส่งจุดเริ่มต้นเข้า Server
                    sendDataToServer(currentLat, currentLon, 0.0, "START");
                } else {
                    double dist = calculateDistance(prevLat, prevLon, currentLat, currentLon);
                    accumulatedDistance += dist;
                    prevLat = currentLat;
                    prevLon = currentLon;

                    Serial.print("Dist: "); Serial.print(dist);
                    Serial.print(" m | Total: "); Serial.println(accumulatedDistance);

                    if (accumulatedDistance >= 0.5) {
                        actionStatus = "DROP";
                        Serial.println(">>> ACTION: DROP! <<<");
                        for(int i=0; i<2; i++){
                            digitalWrite(LED_PIN, HIGH); delay(100);
                            digitalWrite(LED_PIN, LOW);  delay(100);
                        }
                        accumulatedDistance = 0; // Reset
                    }
                    
                    // ส่งข้อมูลขึ้น Server เครื่องพี่
                    sendDataToServer(currentLat, currentLon, accumulatedDistance, actionStatus);
                }
            } else {
                Serial.println("JSON Error");
            }
        }
    }
}