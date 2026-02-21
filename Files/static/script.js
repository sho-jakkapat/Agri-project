// static/script.js

// 1. สร้างแผนที่ (Map)
const map = L.map('mapid').setView([0, 0], 19);

// 2. ใช้ OpenStreetMap (OSM)
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 22,
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

        logs.forEach(log => {
            const lat = parseFloat(log.latitude);
            const lon = parseFloat(log.longitude);
            if (isNaN(lat) || isNaN(lon)) return;
            
            latLngs.push([lat, lon]);

            const status = (log.action_status || "").toString().trim();
            const statusClass = (status === 'Success') ? 'status-success' : 'status-fail';

            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${log.timestamp}</td>
                <td>${log.distance}</td>
                <td class="${statusClass}">${status}</td>
            `;
            tableBody.appendChild(row);
        });

        if (latLngs.length > 0) {
            const pathPoints = [...latLngs].reverse();

            // ==========================================
            // สร้างเส้นทาง (ตรงกลาง + ซ้าย + ขวา)
            // ==========================================
            let leftPoints = [];
            let rightPoints = [];
            let currentHeading = lastHeading;

            for (let i = 0; i < pathPoints.length; i++) {
                let p1 = pathPoints[i];
                let p2 = pathPoints[i+1];

                // หาว่าตอนนี้กำลังหันหน้าไปทิศไหน
                if (p2) {
                    currentHeading = getBearing(p1[0], p1[1], p2[0], p2[1]);
                }

                // หามุมตั้งฉาก ซ้าย (-90) และ ขวา (+90)
                let leftHeading = (currentHeading - 90 + 360) % 360;
                let rightHeading = (currentHeading + 90) % 360;

                // คำนวณพิกัดซ้ายขวาตามระยะ WORK_WIDTH
                leftPoints.push(getOffsetPoint(p1[0], p1[1], leftHeading, WORK_WIDTH));
                rightPoints.push(getOffsetPoint(p1[0], p1[1], rightHeading, WORK_WIDTH));
            }

            // 1. วาดเส้นขนาน ซ้าย-ขวา (เป็นเส้นประสีฟ้าบางๆ)
            L.polyline(leftPoints, { color: '#3498db', weight: 2, dashArray: '10, 10', opacity: 0.6 }).addTo(currentLayers);
            L.polyline(rightPoints, { color: '#3498db', weight: 2, dashArray: '10, 10', opacity: 0.6 }).addTo(currentLayers);

            // 2. วาดเส้นหลักตรงกลาง (เส้นทึบสีแดง)
            L.polyline(pathPoints, { color: '#ff0000', weight: 4, opacity: 0.9 }).addTo(currentLayers);

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