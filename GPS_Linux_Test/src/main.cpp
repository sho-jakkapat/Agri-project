#define _USE_MATH_DEFINES
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> // ต้องใช้สำหรับ HTTPS
#include <cmath>

// --- ⚠️ แก้ชื่อ WiFi ตรงนี้ครับ ⚠️ ---
const char* ssid = "Shox";      
const char* password = "Show2547"; 
// ----------------------------------

// URL หลักของ Cloudflare
const String domain = "https://api.shojakkapat.com";
const String logUrl = domain + "/api/log";
const String clearUrl = domain + "/api/clear"; // API สำหรับล้างข้อมูล

#define LED_PIN 2
double prevLat = 0.0, prevLon = 0.0;
double accumulatedDistance = 0.0;
bool firstFix = true;

// สร้าง Client แบบ Global เพื่อใช้ซ้ำ
WiFiClientSecure client;
HTTPClient http;

// ฟังก์ชันล้างข้อมูลเก่าตอนเริ่มเปิดเครื่อง
void clearServerData() {
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("[SYSTEM] Clearing old database...");
        
        client.setInsecure(); // ข้ามการตรวจใบเซอร์
        
        if (http.begin(client, clearUrl)) {
            // ส่งคำสั่ง DELETE ไปที่ Server
            int httpCode = http.sendRequest("DELETE");
            
            if (httpCode > 0) {
                Serial.printf("[SYSTEM] Database Cleared! Code: %d\n", httpCode);
            } else {
                Serial.printf("[SYSTEM] Clear Failed: %s\n", http.errorToString(httpCode).c_str());
            }
            http.end();
        }
    }
}

// สูตรคำนวณระยะทาง
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
        client.setInsecure();
        
        if (http.begin(client, logUrl)) { 
            http.addHeader("Content-Type", "application/json");

            String jsonPayload = "{";
            jsonPayload += "\"latitude\":" + String(lat, 6) + ",";
            jsonPayload += "\"longitude\":" + String(lon, 6) + ",";
            jsonPayload += "\"gps_fix_status\": 1,";
            jsonPayload += "\"distance\":" + String(dist, 2) + ",";
            jsonPayload += "\"action_status\":\"" + action + "\"";
            jsonPayload += "}";

            // Serial.print("Sending... ");
            int httpResponseCode = http.POST(jsonPayload);

            if(httpResponseCode > 0){
                Serial.printf("OK (%d) | Dist: %.2f | Act: %s\n", httpResponseCode, dist, action.c_str());
            } else {
                Serial.printf("Error: %s\n", http.errorToString(httpResponseCode).c_str());
            }
            http.end();
        }
    } else {
        Serial.println("WiFi Disconnected!");
        WiFi.reconnect();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW); 

    delay(1000);
    Serial.println("\n--- ESP32 Auto-Reset Mode ---");
    
    // เชื่อมต่อ WiFi
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    
    // 🔥 สั่งล้างข้อมูลเก่าทิ้งทันที! 🔥
    clearServerData(); 
    
    Serial.println("Ready! First point will be START.");
}

void loop() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();

        if (input.length() > 0) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, input);

            if (!error) {
                double currentLat = doc["lat"];
                double currentLon = doc["lon"];
                String actionStatus = "FLYING";

                if (firstFix) {
                    // --- พิกัดแรก = จุดเริ่มต้น ---
                    prevLat = currentLat;
                    prevLon = currentLon;
                    firstFix = false;
                    
                    Serial.println(">>> SET HOME POINT <<<");
                    // ส่งจุดแรกเข้า Server
                    sendDataToServer(currentLat, currentLon, 0.0, "START");
                } else {
                    // --- พิกัดต่อไป = คำนวณระยะจากจุดก่อนหน้า ---
                    double dist = calculateDistance(prevLat, prevLon, currentLat, currentLon);
                    accumulatedDistance += dist;
                    prevLat = currentLat;
                    prevLon = currentLon;

                    if (accumulatedDistance >= 0.5) { // เปลี่ยนเป็นระยะที่ต้องการ (เช่น 5 เมตร)
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