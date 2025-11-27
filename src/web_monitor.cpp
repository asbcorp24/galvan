#include <Arduino.h>
#include <pgmspace.h>
#include "web_monitor.h"
const char WEB_UI_MONITOR[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>GalvaControl — Монитор ванн</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        :root {
            --bg: #050814;
            --bg-card: #11172a;
            --accent: #3ba7ff;
            --accent-soft: rgba(59,167,255,0.15);
            --danger: #ff4a4a;
            --text: #f5f7ff;
            --muted: #8b93af;
            --border: #27314a;
        }
        * { box-sizing: border-box; }
        body {
            margin: 0;
            font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
            background: radial-gradient(circle at top left, #1c2845, #050814);
            color: var(--text);
        }
        a { color: inherit; text-decoration: none; }
        .page {
            max-width: 1200px;
            margin: 0 auto;
            padding: 16px;
        }
        header {
            margin-bottom: 12px;
            border-bottom: 1px solid rgba(255,255,255,0.04);
            padding-bottom: 10px;
        }
        .top-row {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-between;
            align-items: center;
            gap: 12px;
        }
        .brand h1 {
            margin: 0;
            font-size: 1.4rem;
            letter-spacing: .06em;
            text-transform: uppercase;
        }
        .brand h1 span { color: var(--accent); }
        .brand .subtitle {
            font-size: .85rem;
            color: var(--muted);
            margin-top: 2px;
        }
        .pill {
            padding: 4px 10px;
            border-radius: 999px;
            border: 1px solid var(--accent-soft);
            background: rgba(5,10,25,0.6);
            font-size: .8rem;
            color: var(--muted);
        }
        nav {
            margin-top: 10px;
            display: flex;
            flex-wrap: wrap;
            gap: 6px;
        }
        .nav-btn {
            border-radius: 999px;
            border: 1px solid var(--border);
            padding: 5px 12px;
            font-size: .8rem;
            background: rgba(255,255,255,0.02);
            display: inline-flex;
            align-items: center;
            gap: 6px;
            cursor: pointer;
        }
        .nav-btn.active {
            border-color: var(--accent);
            background: var(--accent-soft);
        }

        .layout {
            display: grid;
            grid-template-columns: minmax(0, 1.3fr) minmax(0, 1.1fr);
            gap: 16px;
        }
        @media (max-width: 900px) {
            .layout { grid-template-columns: 1fr; }
        }

        .card {
            background: rgba(9, 13, 30, 0.92);
            border-radius: 14px;
            border: 1px solid var(--border);
            padding: 14px 14px 10px;
            box-shadow: 0 18px 40px rgba(0,0,0,.35);
        }
        .card h2 {
            font-size: 1rem;
            margin: 0 0 10px;
            display: flex;
            align-items: center;
            gap: 8px;
        }
        .card h2 span.dot {
            width: 8px;
            height: 8px;
            border-radius: 999px;
            background: var(--accent);
            display: inline-block;
        }

        .baths-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(120px,1fr));
            gap: 8px;
        }
        .bath-card {
            border-radius: 10px;
            border: 1px solid rgba(255,255,255,0.05);
            padding: 8px;
            background: radial-gradient(circle at top left, rgba(59,167,255,0.13), rgba(5,10,25,0.95));
            font-size: .78rem;
        }
        .bath-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 4px;
        }
        .bath-name {
            font-weight: 600;
        }
        .badge {
            padding: 2px 6px;
            border-radius: 999px;
            border: 1px solid rgba(255,255,255,0.1);
            font-size: .7rem;
            color: var(--muted);
        }
        .bath-line {
            display: flex;
            justify-content: space-between;
            margin-top: 2px;
        }
        .bath-label {
            color: var(--muted);
        }
        .bath-value {
            font-variation-settings: "wght" 550;
        }

        canvas {
            width: 100%;
            max-height: 280px;
            background: rgba(3,5,15,0.9);
            border-radius: 10px;
            border: 1px solid rgba(255,255,255,0.06);
        }

        .row {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            align-items: center;
            margin: 4px 0 8px;
        }
        label {
            font-size: .78rem;
            color: var(--muted);
        }
        select, button {
            border-radius: 999px;
            border: 1px solid var(--border);
            padding: 5px 10px;
            font-size: .8rem;
            background: rgba(255,255,255,0.02);
            color: var(--text);
            cursor: pointer;
        }
        button.primary {
            background: linear-gradient(135deg, #3ba7ff, #7a5cff);
            border: none;
        }
        button:disabled { opacity: .6; cursor: default; }

        .msg {
            font-size: .78rem;
            margin-top: 4px;
            min-height: 16px;
        }
        .msg.ok { color: #8fe7b7; }
        .msg.err { color: #ffb4b4; }

        .footer {
            margin-top: 14px;
            font-size: .75rem;
            color: var(--muted);
            text-align: right;
            opacity: .7;
        }
    </style>
</head>
<body>
<div class="page">
    <header>
        <div class="top-row">
            <div class="brand">
                <h1><span>Galva</span>Control</h1>
                <div class="subtitle">Монитор технологических ванн · температура · pH · ток (пример)</div>
            </div>
            <div class="pill">
                Wi-Fi: локальная сеть / ESP32
            </div>
        </div>
        <nav>
            <a href="/" class="nav-btn">🏠 Главная</a>
            <a href="/monitor" class="nav-btn active">📊 Монитор ванн</a>
            <a href="/routes_ui" class="nav-btn">📋 Редактор рецептов</a>
        </nav>
    </header>

    <div class="layout">
        <section class="card">
            <h2><span class="dot"></span> Мгновенные параметры по ваннам</h2>
            <div class="baths-grid" id="bathsGrid">
                <!-- карточки ванн сюда -->
            </div>
        </section>

        <section class="card">
            <h2><span class="dot"></span> История по выбранной ванне</h2>
            <div class="row">
                <label for="bathSelect">Ванна:</label>
                <select id="bathSelect"></select>
                <button class="primary" id="btnReloadHistory">⟳ Обновить график</button>
            </div>
            <canvas id="historyCanvas" width="400" height="260"></canvas>
            <div class="msg" id="monMsg"></div>
        </section>
    </div>

    <div class="footer">
        GalvaControl · прототип мониторинга · реальные датчики подставляются на контроллере
    </div>
</div>

<script>
const el = id => document.getElementById(id);

function createBathCard(b) {
    const div = document.createElement("div");
    div.className = "bath-card";
    div.innerHTML = `
        <div class="bath-header">
            <div class="bath-name">Ванна ${b.id}</div>
            <div class="badge">${b.temp.toFixed(1)} °C</div>
        </div>
        <div class="bath-line">
            <span class="bath-label">pH</span>
            <span class="bath-value">${b.ph.toFixed(2)}</span>
        </div>
        <div class="bath-line">
            <span class="bath-label">ORP, мВ</span>
            <span class="bath-value">${b.orp.toFixed(0)}</span>
        </div>
        <div class="bath-line">
            <span class="bath-label">Ток, А</span>
            <span class="bath-value">${b.current.toFixed(1)}</span>
        </div>
    `;
    return div;
}

async function loadBaths() {
    const grid = el("bathsGrid");
    const msg = el("monMsg");
    msg.className = "msg";
    msg.textContent = "Загрузка текущих параметров...";
    try {
        const res = await fetch("/bath_data");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();
        const baths = (data && data.baths) ? data.baths : [];
        grid.innerHTML = "";
        baths.forEach(b => grid.appendChild(createBathCard(b)));

        // обновим список в выпадающем
        const sel = el("bathSelect");
        const prev = sel.value;
        sel.innerHTML = "";
        baths.forEach(b => {
            const opt = document.createElement("option");
            opt.value = b.id;
            opt.textContent = "Ванна " + b.id;
            sel.appendChild(opt);
        });
        if (prev) sel.value = prev;

        msg.className = "msg ok";
        msg.textContent = "Параметры обновлены";
    } catch (e) {
        console.error(e);
        msg.className = "msg err";
        msg.textContent = "Ошибка запроса /bath_data: " + e.message;
    }
}

// простой график на canvas (температура и ток)
function drawHistoryChart(points) {
    const canvas = el("historyCanvas");
    const ctx = canvas.getContext("2d");
    const w = canvas.width;
    const h = canvas.height;

    ctx.clearRect(0,0,w,h);

    if (!points.length) {
        ctx.fillStyle = "#8b93af";
        ctx.font = "12px system-ui";
        ctx.fillText("Нет данных для отображения", 10, 20);
        return;
    }

    // нормализация
    const temps = points.map(p => p.temp);
    const currents = points.map(p => p.current);
    const tMin = Math.min(...temps);
    const tMax = Math.max(...temps);
    const cMin = Math.min(...currents);
    const cMax = Math.max(...currents);

    const leftPad = 30;
    const rightPad = 10;
    const topPad = 10;
    const bottomPad = 20;

    ctx.fillStyle = "rgba(255,255,255,0.04)";
    ctx.fillRect(leftPad, topPad, w-leftPad-rightPad, h-topPad-bottomPad);

    // сетка
    ctx.strokeStyle = "rgba(255,255,255,0.07)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(leftPad, h-bottomPad);
    ctx.lineTo(w-rightPad, h-bottomPad);
    ctx.stroke();

    // линия температуры
    ctx.beginPath();
    points.forEach((p, i) => {
        const x = leftPad + (w-leftPad-rightPad) * (i/(points.length-1 || 1));
        const normT = (tMax === tMin) ? 0.5 : (p.temp - tMin)/(tMax - tMin);
        const y = topPad + (h-topPad-bottomPad) * (1 - normT);
        if (i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.strokeStyle = "#3ba7ff";
    ctx.lineWidth = 2;
    ctx.stroke();

    // линия тока
    ctx.beginPath();
    points.forEach((p, i) => {
        const x = leftPad + (w-leftPad-rightPad) * (i/(points.length-1 || 1));
        const normC = (cMax === cMin) ? 0.5 : (p.current - cMin)/(cMax - cMin);
        const y = topPad + (h-topPad-bottomPad) * (1 - normC);
        if (i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.strokeStyle = "#ffb74a";
    ctx.lineWidth = 1.5;
    ctx.stroke();

    ctx.fillStyle = "#8b93af";
    ctx.font = "11px system-ui";
    ctx.fillText("Температура", leftPad+4, topPad+12);
    ctx.fillText("Ток", leftPad+4, topPad+26);
}

async function loadHistory() {
    const sel = el("bathSelect");
    const msg = el("monMsg");
    const id = sel.value || 0;

    msg.className = "msg";
    msg.textContent = "Загрузка истории по ванне " + id + "...";

    try {
        const res = await fetch("/bath_history?bath=" + encodeURIComponent(id));
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();
        const points = Array.isArray(data) ? data : [];
        drawHistoryChart(points);
        msg.className = "msg ok";
        msg.textContent = "История обновлена (" + points.length + " точек)";
    } catch (e) {
        console.error(e);
        msg.className = "msg err";
        msg.textContent = "Ошибка истории: " + e.message;
    }
}

document.addEventListener("DOMContentLoaded", () => {
    el("btnReloadHistory").addEventListener("click", loadHistory);
    loadBaths().then(loadHistory);
    setInterval(loadBaths, 5000);
});
</script>
</body>
</html>
)rawliteral";
