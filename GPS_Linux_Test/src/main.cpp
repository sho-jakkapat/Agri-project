#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// 1. ตั้งค่า WiFi และ Web Server
const char* ssid = "Shox";
const char* password = "Show2547";
// เปลี่ยน 192.168.1.xxx เป็น IP ของคอมพิวเตอร์ที่รัน Flask
const String serverUrl = "https://api.shojakkapat.com/api/log"; 

// 2. ตั้งค่าฮาร์ดแวร์ (Motor & RS485)
HardwareSerial MyRS485(1); 
#define PWMA_PIN 14  
#define AIN1_PIN 13  
#define AIN2_PIN 12  

// 3. ตัวแปรเก็บระยะทางและพิกัดอ้างอิง
float last_drop_lat = 0.0;
float last_drop_lon = 0.0;
const float DROP_DISTANCE_METERS = 5.0; // เป้าหมาย: ปล่อยปุ๋ยทุกๆ 5 เมตร

// ฟังก์ชันคำนวณระยะทาง Haversine Formula (หน่วยเป็นเมตร)
float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  if (lat1 == 0.0 && lon1 == 0.0) return 0.0; 
  
  float R = 6371000.0; 
  float dLat = (lat2 - lat1) * PI / 180.0;
  float dLon = (lon2 - lon1) * PI / 180.0;
  float a = sin(dLat/2) * sin(dLat/2) + cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) * sin(dLon/2) * sin(dLon/2);
  float c = 2 * atan2(sqrt(a), sqrt(1-a));
  return R * c; 
}

// ฟังก์ชันสั่งมอเตอร์หยอดปุ๋ย
void dropFertilizer() {
  Serial.println("[MOTOR] Start dropping! Motor ON for 5 seconds...");
  digitalWrite(AIN1_PIN, HIGH);
  digitalWrite(AIN2_PIN, LOW);
  analogWrite(PWMA_PIN, 200); 
  delay(5000); 
  
  Serial.println("[MOTOR] Stop dropping");
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  analogWrite(PWMA_PIN, 0);
}

// ฟังก์ชันส่งข้อมูลขึ้นเว็บ Server
void sendDataToWeb(float lat, float lon, bool is_dropped, float distance) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    
    // API Key ให้ตรงกับ Flask Backend
    doc["api_key"] = "Shiro_Secret_999"; 
    
    doc["latitude"] = lat;
    doc["longitude"] = lon;
    doc["gps_fix_status"] = 1; 
    doc["distance"] = distance;
    
    if (is_dropped) {
        doc["action_status"] = "Dropped"; 
    } else {
        doc["action_status"] = "Moving";  
    }
    
    String requestBody;
    serializeJson(doc, requestBody);
    
    Serial.print("[WEB] Sending JSON: ");
    Serial.println(requestBody); 
    
    int httpResponseCode = http.POST(requestBody);
    
    Serial.print("[WEB] Response Status Code: ");
    if (httpResponseCode == 201) {
        Serial.println("201 (Saved to Database successfully)");
    } else if (httpResponseCode == 401) {
        Serial.println("401 (Unauthorized - API Key mismatch)");
    } else {
        Serial.println(httpResponseCode); 
    }
    
    http.end();
  } else {
    Serial.println("[ERROR] Cannot send data: WiFi disconnected.");
  }
}

// ฟังก์ชันสั่งล้างฐานข้อมูลตอนเปิดเครื่อง
void clearDatabaseOnStartup() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // เปลี่ยน URL ให้ชี้ไปที่ /api/clear
    // อย่าลืมเปลี่ยน IP ให้ตรงกับเครื่องที่รัน Flask
    String clearUrl = "https://api.shojakkapat.com/api/clear"; 
    
    http.begin(clearUrl);
    
    Serial.println("[SYSTEM] Sending command to clear database...");
    
    // ส่ง Request แบบ DELETE
    int httpResponseCode = http.sendRequest("DELETE");
    
    if (httpResponseCode == 200) {
        Serial.println("[SYSTEM] Database cleared successfully! (Code 200)");
    } else {
        Serial.print("[ERROR] Failed to clear database. HTTP Code: ");
        Serial.println(httpResponseCode);
    }
    
    http.end();
  }
}

void setup() {
  Serial.begin(115200);
  // เช็ค Baud Rate ของ RS485 ให้ตรงกับที่ใช้งานจริง (เช่น 9600 หรือ 38400)
  MyRS485.begin(9600, SERIAL_8N1, 8, 16); 
  
  pinMode(PWMA_PIN, OUTPUT);
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  analogWrite(PWMA_PIN, 0);

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  clearDatabaseOnStartup();
}

void loop() {
  if (Serial.available()) {
    String incomingJson = Serial.readStringUntil('\n');
    // ...
    
    if (incomingJson.length() > 0) {
      JsonDocument doc; 
      DeserializationError error = deserializeJson(doc, incomingJson);

      if (!error) {
        float current_lat = doc["lat"];
        float current_lon = doc["lon"];
        bool did_we_drop = false; 
        
        // ถ้าเป็นรอบแรกสุดที่เปิดเครื่อง ให้เซ็ตพิกัดปัจจุบันเป็นจุดเริ่มต้น
        if (last_drop_lat == 0.0 && last_drop_lon == 0.0) {
           last_drop_lat = current_lat;
           last_drop_lon = current_lon;
        }

        // คำนวณระยะทางจากจุดที่หยอดล่าสุด มาถึงจุดปัจจุบัน
        float dist_moved = calculateDistance(last_drop_lat, last_drop_lon, current_lat, current_lon);
        
        Serial.print("[GPS] Lat: "); Serial.print(current_lat, 6);
        Serial.print(" | Lon: "); Serial.print(current_lon, 6);
        Serial.print(" | Distance moved: "); Serial.print(dist_moved); Serial.println(" meters");

        // เช็คว่าระยะทางถึงกำหนดหรือยัง
        if (dist_moved >= DROP_DISTANCE_METERS) {
           Serial.println("[SYSTEM] Target distance reached! Activating motor.");
           dropFertilizer(); 
           
           last_drop_lat = current_lat;
           last_drop_lon = current_lon;
           did_we_drop = true; 
        }

        // ส่งข้อมูลขึ้น Web Server
        sendDataToWeb(current_lat, current_lon, did_we_drop, dist_moved);
        Serial.println("-----------------------------------");
      }
    }
  }
}