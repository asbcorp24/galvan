#include <Arduino.h>
#include "web_layout.h"
#include "web_monitor.h"

const char WEB_MONITOR_CONTENT[] PROGMEM = R"rawliteral(
<div class="container">
    <div id="bathGrid" class="bath-grid"></div>
</div>

<script>
let bathCount = 10;   // можно заменить на значение из ESP32

async function fetchBathData() {
    try {
        const res = await fetch("/bath_data");
        return await res.json();
    } catch(e) {
        return null;
    }
}

async function fetchBathHistory(bath) {
    try {
        const res = await fetch("/bath_history?bath=" + bath);
        return await res.json();
    } catch(e) {
        return [];
    }
}

function drawGraph(canvas, values, color="#3ba7ff") {
    const ctx = canvas.getContext("2d");
    const w = canvas.width = canvas.offsetWidth;
    const h = canvas.height = canvas.offsetHeight;

    ctx.clearRect(0,0,w,h);
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;

    if(values.length < 2) return;

    const max = Math.max(...values);
    const min = Math.min(...values);
    const range = max - min || 1;

    ctx.beginPath();
    for(let i=0;i<values.length;i++){
        const x = i / (values.length-1) * w;
        const y = h - ((values[i]-min)/range)*h;
        if(i===0) ctx.moveTo(x,y);
        else ctx.lineTo(x,y);
    }
    ctx.stroke();
}

async function updateUI() {
    const grid = document.getElementById("bathGrid");
    const data = await fetchBathData();
    if(!data) return;

    grid.innerHTML = "";

    for(let b=0; b<data.baths.length; b++){
        const bath = data.baths[b];
        const card = document.createElement("div");
        card.className = "bath-card";
        card.innerHTML = `
            <h3>Ванна ${bath.id}</h3>
            <div class="row"><span>Температура:</span> <strong>${bath.temp.toFixed(1)}°C</strong></div>
            <div class="row"><span>pH:</span> <strong>${bath.ph.toFixed(2)}</strong></div>
            <div class="row"><span>ORP:</span> <strong>${bath.orp} mV</strong></div>
            <div class="row"><span>Ток:</span> <strong>${bath.current} mA</strong></div>
            <canvas id="graph_${bath.id}"></canvas>
        `;
        grid.appendChild(card);

        // История
        fetchBathHistory(bath.id).then(hist => {
            const values = hist.map(x => x.temp);
            drawGraph(document.getElementById("graph_"+bath.id), values);
        });
    }
}

setInterval(updateUI, 3000);
updateUI();

</script>
)rawliteral";


String renderPageMonitor() {
    String html = FPSTR(WEB_LAYOUT);

    html.replace("{{title}}", "Монитор ванн");
    html.replace("{{nav_monitor}}", "active");

    html.replace("{{nav_index}}", "");
    html.replace("{{nav_routes}}", "");
    html.replace("{{nav_baths}}", "");
    html.replace("{{nav_autolearn}}", "");
    html.replace("{{nav_logs}}", "");

    html.replace("{{content}}", FPSTR(WEB_MONITOR_CONTENT));

    return html;
}
