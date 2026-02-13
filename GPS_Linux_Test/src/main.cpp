#define _USE_MATH_DEFINES
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> // 👈 1. เพิ่ม Library นี้สำหรับ HTTPS
#include <cmath>

// --- ⚠️ แก้ชื่อ WiFi ตรงนี้ครับ ⚠️ ---
const char* ssid = "Shox";      
const char* password = "Show2547"; 
// ----------------------------------

// 👈 2. แก้ URL เป็นโดเมน Cloudflare ของพี่ (ต้องเป็น https)
const String serverUrl = "https://api.shojakkapat.com/api/log";

#define LED_PIN 2
double prevLat = 0.0, prevLon = 0.0;
double accumulatedDistance = 0.0;
bool firstFix = true;

// สูตรคำนวณระยะทาง (Haversine Formula) - ของเดิมพี่
double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    double R = 6371000; 
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a)); 
    return R * c;
}

void sendDataToServer(double lat, double lon, double dist, String action) {
    if(WiFi.status() == WL_CONNECTED){
        
        // 👈 3. สร้าง Client แบบ Secure (สำคัญมาก!)
        WiFiClientSecure client;
        client.setInsecure(); // สั่งให้ไม่ต้องตรวจใบ Certificate (เพื่อให้ผ่าน Cloudflare ได้ง่ายๆ)

        HTTPClient http;
        
        // ใช้ begin แบบใส่ client เข้าไปด้วย
        if (http.begin(client, serverUrl)) { 
            http.addHeader("Content-Type", "application/json");

            // สร้าง JSON ให้ตรงกับ app.py (ของเดิมพี่ถูกต้องแล้ว)
            String jsonPayload = "{";
            jsonPayload += "\"latitude\":" + String(lat, 6) + ",";
            jsonPayload += "\"longitude\":" + String(lon, 6) + ",";
            jsonPayload += "\"gps_fix_status\": 1,";
            jsonPayload += "\"distance\":" + String(dist, 2) + ",";
            jsonPayload += "\"action_status\":\"" + action + "\"";
            jsonPayload += "}";

            Serial.print("Sending to Cloudflare... ");
            int httpResponseCode = http.POST(jsonPayload);

            if(httpResponseCode > 0){
                Serial.println("OK! Code: " + String(httpResponseCode));
                // Serial.println(http.getString()); // เปิดบรรทัดนี้ถ้าอยากเห็นข้อความตอบกลับ
            } else {
                Serial.print("Error: ");
                // ปริ้นท์ Error แบบละเอียดออกมาดู
                Serial.println(http.errorToString(httpResponseCode).c_str());
            }
            http.end();
        } else {
            Serial.println("Unable to connect to Server URL");
        }
    } else {
        Serial.println("WiFi Disconnected!");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); 

    delay(1000);
    Serial.println("\n--- ESP32 Cloudflare Mode ---");
    
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
    
    Serial.println("Ready! Type JSON to simulate GPS (e.g., {\"lat\":13.7, \"lon\":100.5})");
}

void loop() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        if (input.length() > 0) {
            Serial.print("CHECK INPUT: "); Serial.println(input);

            JsonDocument doc; // ใช้ JsonDocument แบบใหม่ (ArduinoJson v7) หรือ StaticJsonDocument ก็ได้
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
                        accumulatedDistance = 0; 
                    }
                    
                    sendDataToServer(currentLat, currentLon, accumulatedDistance, actionStatus);
                }
            } else {
                Serial.println("JSON Error");
            }
        }
    }
}