import serial
import requests
import json
import time

# --- ตั้งค่า ---
# ใช้ r'\\.\COM_2' เพื่อแก้ปัญหา Windows มองไม่เห็นชื่อที่มีขีดล่าง
SERIAL_PORT = r'\\.\COM_2' 
BAUD_RATE = 9600

# ลิงก์ไปยัง app.py
API_URL = "http://127.0.0.1:5000/api/log"
API_CLEAR = "http://127.0.0.1:5000/api/clear"

# --- 1. สั่งล้างข้อมูลเก่าทิ้งก่อนเริ่มงาน ---
print("🧹 Cleaning old data...")
try:
    # ยิงคำสั่งไปบอก app.py ให้ลบข้อมูล
    requests.delete(API_CLEAR)
    print("✨ Database Cleared! Ready to start.")
except Exception as e:
    print(f"⚠️ Warning: Could not clear database. Is app.py running? ({e})")

# --- 2. เชื่อมต่อ Serial ---
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    print(f"✅ Serial Bridge Started on {SERIAL_PORT}")
    print("Waiting for Rover data...")
except Exception as e:
    print(f"❌ Error: Could not open port {SERIAL_PORT}")
    print("💡 Tip: Check com0com names again.")
    exit()

# --- 3. วนลูปรับข้อมูล ---
while True:
    if ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8').strip()
            if not line: continue
            
            # แปลงเป็น JSON
            data = json.loads(line)
            
            # ส่งเข้า Server
            resp = requests.post(API_URL, json=data)
            
            if resp.status_code == 201:
                print(f"📍 Saved: Lat {data['latitude']}, Lon {data['longitude']}")
            else:
                print(f"❌ Server Error: {resp.status_code} - {resp.text}")
                
        except json.JSONDecodeError:
            print("⚠️ Received garbage data (JSON Error)")
        except Exception as e:
            print(f"❌ Error: {e}")