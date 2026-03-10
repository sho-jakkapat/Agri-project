#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// 1. ตั้งค่า WiFi และ Web Server
const char* ssid = "Shox";
const char* password = "Show2547";
const String serverUrl = "http://api.shojakkapat.com/api/log"; 
const String clearUrl = "http://api.shojakkapat.com/api/clear"; 

// 2. ตั้งค่าฮาร์ดแวร์
HardwareSerial MyRS485(1); 
#define PWMA_PIN 14  
#define AIN1_PIN 13  
#define AIN2_PIN 12  
#define LED_PIN 40       
#define ENCODER_PIN 5    

// 3. ตัวแปรระยะทางและเกณฑ์การปล่อยปุ๋ย (อัปเดตใหม่สำหรับ RTK)
double ref_gps_lat = 0.0; // เก็บพิกัดอ้างอิงล่าสุด
double ref_gps_lon = 0.0; 
const float DROP_DISTANCE_METERS = 0.5; // ระยะห่างที่ต้องการ (50 ซม.)
float current_target_distance = DROP_DISTANCE_METERS; // เป้าหมายที่ขยับได้เพื่อชดเชยระยะที่เกิน
const int MIN_PULSES_FOR_SUCCESS = 0; 

volatile long pulseCount = 0;

void IRAM_ATTR countPulse() {
  pulseCount++;
}

// 4. โครงสร้างข้อมูลสำหรับส่งผ่านท่อ Queue ระหว่าง Core 1 ไป Core 0
struct GPSData {
  double lat;       
  double lon;       
  float distance;
  char status[16]; 
};

// สร้างตัวแปรท่อส่งข้อมูล
QueueHandle_t dataQueue;

// --- ฟังก์ชันช่วยเหลือพื้นฐาน ---
double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  if (lat1 == 0.0 && lon1 == 0.0) return 0.0; 
  double R = 6371000.0; 
  double dLat = (lat2 - lat1) * PI / 180.0;
  double dLon = (lon2 - lon1) * PI / 180.0;
  double a = sin(dLat/2) * sin(dLat/2) + cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) * sin(dLon/2) * sin(dLon/2);
  double c = 2 * atan2(sqrt(a), sqrt(1-a));
  return R * c; 
}

void clearDatabaseOnStartup() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(clearUrl);
    Serial.println("[SYSTEM] Sending command to clear database...");
    int httpResponseCode = http.sendRequest("DELETE");
    if (httpResponseCode == 200) {
        Serial.println("[SYSTEM] Database cleared successfully!");
    } else {
        Serial.print("[ERROR] Failed to clear database. HTTP Code: ");
        Serial.println(httpResponseCode);
    }
    http.end();
  }
}

bool dropFertilizer() {
  pulseCount = 0; 
  
  Serial.println("[MOTOR] Start dropping! (Reverse Direction)");
  digitalWrite(LED_PIN, HIGH); 
  
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, HIGH);
  analogWrite(PWMA_PIN, 255); 
  delay(300); 
  
  Serial.println("[MOTOR] Stop dropping");
  digitalWrite(LED_PIN, LOW);
  
  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  analogWrite(PWMA_PIN, 0);

  Serial.print("[FC-33 ENCODER] Pulses detected: ");
  Serial.println(pulseCount);

  if (pulseCount >= MIN_PULSES_FOR_SUCCESS) {
      Serial.println("[STATUS] Drop SUCCESS!");
      return true;
  } else {
      Serial.println("[STATUS] Drop FAILED! (Motor jam or sensor issue)");
      return false;
  }
}

void sendDataToWeb(double lat, double lon, String action_status, float distance) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    JsonDocument doc;
    doc["api_key"] = "Shiro_Secret_999"; 
    doc["latitude"] = lat;
    doc["longitude"] = lon;
    doc["gps_fix_status"] = 1; 
    doc["distance"] = distance;
    doc["action_status"] = action_status; 
    
    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpResponseCode = http.POST(requestBody);
    if (httpResponseCode == 201) {
        Serial.println("[WEB] Saved to Database successfully");
    } else {
        Serial.print("[WEB] Error HTTP Code: ");
        Serial.println(httpResponseCode); 
    }
    http.end();
  } else {
    Serial.println("[ERROR] Cannot send data: WiFi disconnected.");
  }
}

// ==========================================
// 🚀 TASK 1: งานส่งข้อมูลขึ้นเว็บ (ทำงานบน Core 0)
// ==========================================
void NetworkTask(void * pvParameters) {
  GPSData receivedData;
  for(;;) {
    if (xQueueReceive(dataQueue, &receivedData, portMAX_DELAY) == pdPASS) {
      sendDataToWeb(receivedData.lat, receivedData.lon, String(receivedData.status), receivedData.distance);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// 🚜 TASK 2: งานคุมฮาร์ดแวร์หลัก (ทำงานบน Core 1)
// ==========================================
void HardwareTask(void * pvParameters) {
  for(;;) {
    if (MyRS485.available()) {
      
      String incomingJson = MyRS485.readStringUntil('\n');
      incomingJson.trim(); 
      
      if (incomingJson.length() > 0) {
        Serial.print(">> [RS485 RAW DATA]: ");
        Serial.println(incomingJson);

        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, incomingJson);

        if (!error) {
          double current_lat = doc["lat"];
          double current_lon = doc["lon"];
          String current_status = "Moving"; 
          
          // 1. ถ้าเปิดเครื่องครั้งแรก ให้จำจุดสตาร์ทเป็น 'จุดอ้างอิง'
          if (ref_gps_lat == 0.0 && ref_gps_lon == 0.0) {
             ref_gps_lat = current_lat;
             ref_gps_lon = current_lon;
          }

          // 2. วัดระยะห่างจาก 'จุดอ้างอิง' มายัง 'จุดปัจจุบัน'
          double distance_from_ref = calculateDistance(ref_gps_lat, ref_gps_lon, current_lat, current_lon);
          
          Serial.print("[GPS] Lat: "); Serial.print(current_lat, 8);
          Serial.print(" | Lon: "); Serial.print(current_lon, 8);
          Serial.print(" | Dist from Ref: "); Serial.print(distance_from_ref, 4);
          Serial.print(" | Target: "); Serial.println(current_target_distance, 4);

          // 3. ถ้าระยะห่างถึงเป้าหมายปัจจุบัน (มีการหักลบเศษความคลาดเคลื่อนแล้ว)
          if (distance_from_ref >= current_target_distance) {
             Serial.println("[SYSTEM] Target distance reached! Activating motor.");
             
             bool is_success = dropFertilizer(); 
             if (is_success) {
                 current_status = "Dropped";
             } else {
                 current_status = "Drop Failed";
             }
             
             // 4. หักลบเศษที่เกินมา (Excess) เพื่อนำไปลดระยะเป้าหมายในรอบถัดไป
             float excess = distance_from_ref - current_target_distance;
             
             // ป้องกันกรณี GPS กระโดดไกลผิดปกติ (เผื่อสัญญาณหายชั่วขณะ)
             if (excess >= DROP_DISTANCE_METERS) {
                 excess = 0; 
             }

             // อัปเดตจุดอ้างอิงใหม่เป็นตำแหน่งปัจจุบัน
             ref_gps_lat = current_lat;
             ref_gps_lon = current_lon;
             
             // คำนวณเป้าหมายรอบถัดไป (เอาระยะ 0.5 หักลบด้วยเศษที่เกินมา)
             current_target_distance = DROP_DISTANCE_METERS - excess;
          }

          GPSData dataToSend;
          dataToSend.lat = current_lat;
          dataToSend.lon = current_lon;
          dataToSend.distance = distance_from_ref; 
          strlcpy(dataToSend.status, current_status.c_str(), sizeof(dataToSend.status));
          
          xQueueSend(dataQueue, &dataToSend, portMAX_DELAY);
          Serial.println("-----------------------------------");
          
        } else {
          Serial.print("<< [JSON ERROR]: ");
          Serial.println(error.c_str());
          Serial.println("-----------------------------------");
        }
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS); 
  }
}

void setup() {
  Serial.begin(115200);
  
  MyRS485.begin(230400, SERIAL_8N1, 17, 18); 
  
  pinMode(PWMA_PIN, OUTPUT);
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  pinMode(ENCODER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN), countPulse, RISING);

  digitalWrite(AIN1_PIN, LOW);
  digitalWrite(AIN2_PIN, LOW);
  analogWrite(PWMA_PIN, 0);
  digitalWrite(LED_PIN, LOW);

  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  
  clearDatabaseOnStartup();
  
  dataQueue = xQueueCreate(10, sizeof(GPSData));

  xTaskCreatePinnedToCore(NetworkTask, "NetTask", 8192, NULL, 1, NULL, 0); 
  xTaskCreatePinnedToCore(HardwareTask, "HWTask", 8192, NULL, 1, NULL, 1); 

  Serial.println("--- ShiroQuest RTOS Master Control Ready ---");
}

void loop() {
  vTaskDelete(NULL); 
}