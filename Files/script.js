// static/script.js


const map = L.map('mapid').setView([0, 0], 2);

L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 25,
    attribution: '© OpenStreetMap contributors'
}).addTo(map);

let currentLayers = L.layerGroup().addTo(map);
let isFirstLoad = true;

async function fetchAndPlotLogs() {
    try {
        const response = await fetch('/api/logs');
        const data = await response.json();
        const logs = data.logs;

        // ล้างของเก่า
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

            let color;
            const status = (log.action_status || "").toString().trim();

            if (status !== 'Success') {
                color = '#e74c3c';
            }
            else if (log.gps_fix_status === 0) {
                color = '#2ecc71';
            }
            else {
                color = '#f39c12';
            }
            // ==========================================

            const popupContent = `
                <div style="text-align:center">
                    <b>No:</b> ${log.id} | <b>Time:</b> ${log.timestamp}<br>
                    <b>Lat:</b> ${lat.toFixed(6)} <b>Lon:</b> ${lon.toFixed(6)}<br>
                    <b>Distance:</b> ${log.distance} m<br>
                    <b>Status:</b> <span style="color:${color}; font-weight:bold">${status}</span>
                </div>
            `;

            L.circleMarker([lat, lon], {
                radius: 6,
                fillColor: color,
                color: "#333",
                weight: 1,
                fillOpacity: 0.9
            })
                .bindPopup(popupContent)
                .addTo(currentLayers);

            // เติมตาราง
            const row = document.createElement('tr');
            // เช็ค class สำหรับสีตัวหนังสือในตาราง
            const statusClass = (status === 'Success') ? 'status-success' : 'status-fail';

            row.innerHTML = `
                <td>${log.timestamp}</td>
                <td>${log.distance}</td>
                <td class="${statusClass}">${status}</td>
            `;
            tableBody.appendChild(row);
        });

        // วาดเส้น
        if (latLngs.length > 0) {
            const pathPoints = [...latLngs].reverse();
            L.polyline(pathPoints, { color: '#3498db', weight: 3 }).addTo(currentLayers);

            if (isFirstLoad) {
                map.fitBounds(pathPoints, { padding: [50, 50] });
                isFirstLoad = false;
            }
        }
    } catch (error) {
        console.error("Error loading map data:", error);
    }
}

fetchAndPlotLogs();
setInterval(fetchAndPlotLogs, 3000);