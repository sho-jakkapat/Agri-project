// static/script.js

// 1. สร้างแผนที่ (Map)
const map = L.map('mapid').setView([0, 0], 19);

// 2. ใช้ OpenStreetMap (OSM)
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 25,
    maxNativeZoom: 19,
    attribution: '© OpenStreetMap contributors'
}).addTo(map);

let currentLayers = L.layerGroup().addTo(map);
let isFirstLoad = true;
let lastHeading = 0; 

// --- ตั้งค่าระยะเส้นขนาน (ซ้าย-ขวา) ---
const WORK_WIDTH = 2; // ระยะเส้นข้างห่างจากตัวรถ 2 เมตร (พี่แก้เลขตรงนี้ได้เลย)

// 3. ฟังก์ชันคำนวณทิศ (Bearing)
function getBearing(startLat, startLng, destLat, destLng) {
    const startLatRad = startLat * Math.PI / 180;
    const startLngRad = startLng * Math.PI / 180;
    const destLatRad = destLat * Math.PI / 180;
    const destLngRad = destLng * Math.PI / 180;

    const y = Math.sin(destLngRad - startLngRad) * Math.cos(destLatRad);
    const x = Math.cos(startLatRad) * Math.sin(destLatRad) -
        Math.sin(startLatRad) * Math.cos(destLatRad) * Math.cos(destLngRad - startLngRad);

    let brng = Math.atan2(y, x);
    return (brng * 180 / Math.PI + 360) % 360; 
}

// 4. ฟังก์ชันหาพิกัดตั้งฉาก (สร้างเส้นข้าง)
function getOffsetPoint(lat, lng, bearing, distance) {
    const R = 6378137; // รัศมีโลก (เมตร)
    const brng = bearing * Math.PI / 180;
    const lat1 = lat * Math.PI / 180;
    const lon1 = lng * Math.PI / 180;

    const lat2 = Math.asin(Math.sin(lat1) * Math.cos(distance / R) +
                         Math.cos(lat1) * Math.sin(distance / R) * Math.cos(brng));
    const lon2 = lon1 + Math.atan2(Math.sin(brng) * Math.sin(distance / R) * Math.cos(lat1),
                                 Math.cos(distance / R) - Math.sin(lat1) * Math.sin(lat2));

    return [lat2 * 180 / Math.PI, lon2 * 180 / Math.PI];
}

// 5. รูปไอคอนลูกศร (จรวดนำทาง)
const arrowSvg = `
<div class="vehicle-arrow" style="width: 40px; height: 40px; display: flex; align-items: center; justify-content: center;">
    <svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg">
        <path d="M12 2L2 22L12 18L22 22L12 2Z" fill="#FF0000" stroke="white" stroke-width="2"/>
    </svg>
</div>`;

async function fetchAndPlotLogs() {
    try {
        const response = await fetch('/api/logs');
        const data = await response.json();
        const logs = data.logs;

        currentLayers.clearLayers();
        const tableBody = document.getElementById('logTableBody');
        tableBody.innerHTML = "";

        if (!logs || logs.length === 0) return;

        const latLngs = [];
        const dropLogs = []; 
        
        // ดึงสถานะของ Checkbox ว่าให้โชว์เฉพาะ Dropped ไหม
        const filterCheckbox = document.getElementById('filterDropOnly');
        const showOnlyDropped = filterCheckbox ? filterCheckbox.checked : false;

        logs.forEach(log => {
            const lat = parseFloat(log.latitude);
            const lon = parseFloat(log.longitude);
            if (isNaN(lat) || isNaN(lon)) return;
            
            latLngs.push([lat, lon]);

            const status = (log.action_status || "").toString().trim();
            
            // แปลงหน่วยจากเมตรเป็นเซนติเมตร (คูณ 100)
            const distanceCm = (parseFloat(log.distance) * 100).toFixed(2);
            
            // 1. จัดการตาราง (เพิ่มเงื่อนไขการกรอง)
            if (status === 'Dropped' || status === 'Moving') {
                // ถ้าติ๊ก Checkbox แล้วสถานะไม่ใช่ Dropped ให้ข้ามการวาดลงตารางไปเลย
                if (showOnlyDropped && status !== 'Dropped') {
                    // ไม่ทำอะไร (ข้าม)
                } else {
                    const statusClass = (status === 'Dropped') ? 'status-success' : 'status-fail';
                    const row = document.createElement('tr');
                    
                    row.innerHTML = `
                        <td>${log.timestamp}</td>
                        <td>${distanceCm}</td>
                        <td class="${statusClass}">${status}</td>
                    `;
                    tableBody.appendChild(row);
                }
            }

            // 2. เก็บ log ที่เป็น Dropped เอาไว้วาดทีหลังสุด
            if (status === 'Dropped') {
                // เอาระยะที่แปลงเป็นเซนแล้วไปแปะไว้ใน object ด้วย จะได้ดึงไปโชว์ใน popup ง่ายๆ
                log.distanceCm = distanceCm; 
                dropLogs.push(log);
            }
        });

        if (latLngs.length > 0) {
            const pathPoints = [...latLngs].reverse();

            // ==========================================
            // วาดเส้นทาง (ตรงกลาง + ซ้าย + ขวา)
            // ==========================================
            let leftPoints = [];
            let rightPoints = [];
            let currentHeading = lastHeading;

            for (let i = 0; i < pathPoints.length; i++) {
                let p1 = pathPoints[i];
                let p2 = pathPoints[i+1];
                if (p2) currentHeading = getBearing(p1[0], p1[1], p2[0], p2[1]);

                let leftHeading = (currentHeading - 90 + 360) % 360;
                let rightHeading = (currentHeading + 90) % 360;

                leftPoints.push(getOffsetPoint(p1[0], p1[1], leftHeading, WORK_WIDTH));
                rightPoints.push(getOffsetPoint(p1[0], p1[1], rightHeading, WORK_WIDTH));
            }

            L.polyline(leftPoints, { color: '#3498db', weight: 2, dashArray: '10, 10', opacity: 0.6 }).addTo(currentLayers);
            L.polyline(rightPoints, { color: '#3498db', weight: 2, dashArray: '10, 10', opacity: 0.6 }).addTo(currentLayers);
            L.polyline(pathPoints, { color: '#ff0000', weight: 4, opacity: 0.9 }).addTo(currentLayers);

            // ==========================================
            // วาดจุด Marker วงกลมสีเขียว
            // ==========================================
            dropLogs.forEach(log => {
                L.circleMarker([parseFloat(log.latitude), parseFloat(log.longitude)], {
                    color: '#27ae60',
                    fillColor: '#2ecc71',
                    fillOpacity: 1,
                    radius: 8,
                    weight: 2
                })
                .bindPopup(`
                    <div style="text-align: center; font-family: 'Segoe UI', Tahoma, sans-serif;">
                        <b style="color: #27ae60; font-size: 14px;">🌱 ปล่อยปุ๋ยสำเร็จ</b><br>
                        <hr style="margin: 5px 0; border: 0; border-top: 1px solid #eee;">
                        <b>เวลา:</b> ${log.timestamp}<br>
                        <b>ระยะที่ได้:</b> ${log.distanceCm} ซม.
                    </div>
                `)
                .addTo(currentLayers);
            });

            // ==========================================
            // วาดลูกศรหน้ารถ
            // ==========================================
            const latestPoint = pathPoints[pathPoints.length - 1];
            const prevPoint = pathPoints[pathPoints.length - 2];

            let heading = lastHeading;
            if (prevPoint) {
                heading = getBearing(prevPoint[0], prevPoint[1], latestPoint[0], latestPoint[1]);
                lastHeading = heading;
            }

            const arrowIcon = L.divIcon({
                className: 'custom-arrow',
                html: `<div style="transform: rotate(${heading}deg); width: 40px; height: 40px;">${arrowSvg}</div>`,
                iconSize: [40, 40],
                iconAnchor: [20, 20]
            });

            L.marker(latestPoint, { icon: arrowIcon }).addTo(currentLayers);

            if (isFirstLoad) {
                map.setView(latestPoint, 20); 
                isFirstLoad = false;
            } else {
                map.panTo(latestPoint);
            }
        }
    } catch (error) {
        console.error("Error loading map data:", error);
    }
}

fetchAndPlotLogs();
setInterval(fetchAndPlotLogs, 1000);