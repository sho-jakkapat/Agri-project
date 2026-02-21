import requests
import time

# URL ของ Server (แก้ตามจริงถ้าไม่ได้รันในเครื่องตัวเอง)
SERVER_URL = "http://localhost:5000/api/log" 

def create_lawnmower_path():
    path = []
    lat, lon = 13.729100, 100.775400
    dist = 0.0

    def add_point(d_lat, d_lon, status="FLYING"):
        nonlocal lat, lon, dist
        lat += d_lat
        lon += d_lon
        dist += 1.0 # สมมติว่าเดินไปเรื่อยๆ ระยะทางเพิ่มขึ้น
        path.append({
            "latitude": lat,
            "longitude": lon,
            "distance": dist,
            "action_status": status
        })

    # 1. วิ่งตรงไปทางเหนือ (Straight Line)
    for _ in range(10):
        add_point(0.000020, 0)
    
    path[-1]["action_status"] = "DROP" # ดรอปของสุดซอย

    # 2. ตีโค้ง U-Turn ขวาแบบสมูท (Smooth Right Turn)
    for _ in range(4): # โค้งออก
        add_point(0.000010, 0.000005)
    for _ in range(4): # โค้งเข้า
        add_point(-0.000010, 0.000005)

    # 3. วิ่งตรงกลับลงมาทางใต้ (Parallel Line)
    for _ in range(10):
        add_point(-0.000020, 0)
        
    path[-1]["action_status"] = "DROP"

    # 4. ตีโค้ง U-Turn ซ้ายแบบสมูท (Smooth Left Turn)
    for _ in range(4): # โค้งออก
        add_point(-0.000010, 0.000005)
    for _ in range(4): # โค้งเข้า
        add_point(0.000010, 0.000005)

    # 5. วิ่งตรงขึ้นเหนืออีกรอบ เลนที่ 3
    for _ in range(10):
        add_point(0.000020, 0)

    path[-1]["action_status"] = "Success" # เสร็จสิ้นภารกิจ

    return path

# สร้างชุดข้อมูล
points = create_lawnmower_path()

print(f"🚜 เริ่มจำลองการขับรถไถ (Lawnmower Pattern) จำนวน {len(points)} จุด...")
print(">> ให้เปิดหน้าเว็บดู ลูกศรจะค่อยๆ หมุนตีโค้ง U-Turn ครับ <<\n")

for i, p in enumerate(points):
    payload = {
        "latitude": p["latitude"],
        "longitude": p["longitude"],
        "gps_fix_status": 1,
        "distance": round(p["distance"], 2),
        "action_status": p["action_status"]
    }
    
    try:
        res = requests.post(SERVER_URL, json=payload)
        print(f"[{i+1:02d}/{len(points)}] ยิงพิกัดไปที่ {p['latitude']:.6f}, {p['longitude']:.6f} | สถานะ: {p['action_status']}")
    except Exception as e:
        print(f"❌ ส่งข้อมูลล้มเหลว: {e}")
        
    time.sleep(1.0) # หน่วงเวลา 1 วิ ให้หน้าเว็บลากเส้นและหมุนลูกศรให้ดูสมูท

print("\n✅ จบการจำลอง (เดินครบรอบแล้ว!)")