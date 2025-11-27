#include "web_ui.h"

const char WEB_UI_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>GalvaControl — Панель управления</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        :root {
            --bg: #0b1320;
            --bg-card: #151d30;
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
        .page {
            max-width: 1200px;
            margin: 0 auto;
            padding: 16px;
        }
        header {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-between;
            align-items: center;
            gap: 12px;
            margin-bottom: 10px;
        }
        header h1 {
            margin: 0;
            font-size: 1.4rem;
            letter-spacing: .05em;
            text-transform: uppercase;
        }
        header h1 span {
            color: var(--accent);
        }
        header .subtitle {
            font-size: .85rem;
            color: var(--muted);
        }
        .pill {
            padding: 4px 10px;
            border-radius: 999px;
            border: 1px solid var(--accent-soft);
            background: rgba(5,10,25,0.6);
            font-size: .8rem;
            color: var(--muted);
        }

        /* Навигация */
        .nav-bar {
            display: flex;
            gap: 8px;
            margin-bottom: 14px;
            flex-wrap: wrap;
        }
        .nav-link {
            padding: 6px 14px;
            border-radius: 999px;
            border: 1px solid var(--border);
            font-size: .82rem;
            text-decoration: none;
            color: var(--muted);
            background: rgba(6,10,24,0.7);
            display: inline-flex;
            align-items: center;
            gap: 6px;
        }
        .nav-link span.icon {
            font-size: .95rem;
        }
        .nav-link:hover {
            border-color: var(--accent-soft);
        }
        .nav-link.active {
            color: var(--text);
            border-color: var(--accent);
            background: linear-gradient(135deg, #3ba7ff33, #7a5cff22);
        }

        .layout {
            display: grid;
            grid-template-columns: minmax(0, 1.1fr) minmax(0, 1.6fr);
            gap: 16px;
        }
        @media (max-width: 900px) {
            .layout {
                grid-template-columns: 1fr;
            }
        }
        .card {
            background: rgba(9, 13, 30, 0.9);
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
            display: inline-block;
            width: 8px;
            height: 8px;
            border-radius: 999px;
            background: var(--accent);
        }
        .row {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            margin: 6px 0;
        }
        .row > div {
            flex: 1 1 120px;
            min-width: 0;
        }
        .label {
            font-size: .7rem;
            text-transform: uppercase;
            letter-spacing: .08em;
            color: var(--muted);
            margin-bottom: 2px;
        }
        .value {
            font-size: .95rem;
        }
        .value.large {
            font-size: 1.2rem;
            font-weight: 600;
        }
        .value.badge {
            display: inline-flex;
            align-items: center;
            padding: 3px 8px;
            border-radius: 999px;
            background: rgba(255,255,255,0.03);
            border: 1px solid var(--border);
            font-size: .78rem;
            gap: 6px;
        }
        .value.badge span.indicator {
            display: inline-block;
            width: 7px;
            height: 7px;
            border-radius: 999px;
            background: var(--accent);
        }
        .btn-row {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            margin-top: 6px;
        }
        button {
            border-radius: 999px;
            border: none;
            padding: 7px 14px;
            font-size: .85rem;
            cursor: pointer;
            display: inline-flex;
            align-items: center;
            gap: 6px;
            background: rgba(255,255,255,0.03);
            color: var(--text);
            transition: background .15s, transform .05s;
        }
        button span.icon {
            font-size: 1rem;
        }
        button.primary {
            background: linear-gradient(135deg, #3ba7ff, #7a5cff);
        }
        button.danger {
            background: rgba(255,74,74,0.1);
            color: #ffb4b4;
            border: 1px solid rgba(255,74,74,0.4);
        }
        button.secondary {
            border: 1px solid var(--border);
        }
        button:active {
            transform: translateY(1px);
        }
        button:disabled {
            opacity: .5;
            cursor: default;
        }
        .status-bar {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            margin-top: 8px;
            font-size: .78rem;
            color: var(--muted);
        }
        .chip {
            padding: 3px 8px;
            border-radius: 999px;
            border: 1px solid var(--border);
            background: rgba(255,255,255,0.02);
        }
        .msg {
            font-size: .78rem;
            margin-top: 4px;
            min-height: 16px;
        }
        .msg.ok { color: #8fe7b7; }
        .msg.err { color: #ffb4b4; }
        .footer {
            margin-top: 10px;
            font-size: .75rem;
            color: var(--muted);
            text-align: right;
            opacity: .7;
        }
        .pill-soft {
            display: inline-block;
            padding: 3px 8px;
            border-radius: 999px;
            border: 1px solid var(--border);
            background: rgba(255,255,255,0.02);
            font-size: .78rem;
            color: var(--muted);
        }
    </style>
</head>
<body>
<div class="page">
    <header>
        <div>
            <h1><span>Galva</span>Control</h1>
            <div class="subtitle">Панель управления линией гальваники с магнитной разметкой</div>
        </div>
        <div class="pill">
            Wi-Fi: <span id="wifiInfo">ESP32 AP / локальная сеть</span>
        </div>
    </header>

    <nav class="nav-bar">
        <a href="/" class="nav-link active"><span class="icon">⚙</span>Панель управления</a>
        <a href="/monitor" class="nav-link"><span class="icon">📈</span>Монитор ванн</a>
        <a href="/routes_ui" class="nav-link"><span class="icon">📋</span>Рецепты / паттерны</a>
    </nav>

    <div class="layout">
        <!-- Левая карта: текущее состояние -->
        <section class="card">
            <h2><span class="dot"></span> Текущее состояние</h2>
            <div class="row">
                <div>
                    <div class="label">Время контроллера</div>
                    <div class="value large" id="stTime">--:--:--</div>
                    <div class="value" id="stDate">----------</div>
                </div>
                <div>
                    <div class="label">Состояние</div>
                    <div class="value badge" id="stState">
                        <span class="indicator"></span>
                        <span>нет данных</span>
                    </div>
                </div>
            </div>

            <div class="row">
                <div>
                    <div class="label">Текущая ванна</div>
                    <div class="value large" id="stBath">–</div>
                </div>
                <div>
                    <div class="label">Шаг</div>
                    <div class="value large" id="stStep">–</div>
                    <div class="value" id="stStepsTotal">/ –</div>
                </div>
            </div>

            <div class="row">
                <div>
                    <div class="label">Температура</div>
                    <div class="value" id="stTemp">–</div>
                </div>
                <div>
                    <div class="label">pH</div>
                    <div class="value" id="stPh">–</div>
                </div>
            </div>

            <div class="btn-row">
                <button class="primary" id="btnStart">
                    <span class="icon">▶</span>
                    Запустить цикл
                </button>
                <button class="danger" id="btnStop">
                    <span class="icon">⏹</span>
                    Остановить
                </button>
                <button class="secondary" id="btnRefresh">⟳ Обновить статус</button>
            </div>

            <div class="status-bar">
                <span class="chip">Последний запрос: <span id="stLastReq">нет</span></span>
                <span class="chip">Ответ: <span id="stLastStatus">—</span></span>
            </div>
        </section>

        <!-- Правая карта: выбор активного рецепта -->
        <section class="card">
            <h2><span class="dot"></span> Рецепт / паттерн для запуска</h2>

            <div class="row">
                <div>
                    <div class="label">Активный рецепт в контроллере</div>
                    <div class="value large" id="activeRouteLabel">–</div>
                    <div class="value">
                        <span class="pill-soft">ID активного: <span id="activeRouteId">–</span></span>
                    </div>
                </div>
                <div>
                    <div class="label">Выбрать рецепт из библиотеки</div>
                    <select id="routeSelect">
                        <option value="">— нет данных —</option>
                    </select>
                    <div class="value" style="margin-top:4px;font-size:.78rem;color:var(--muted);">
                        Библиотека рецептов живёт в NVS — выбираем ID и применяем.
                    </div>
                </div>
            </div>

            <div class="btn-row" style="margin-top:10px;">
                <button class="primary" id="btnApplyRoute">
                    <span class="icon">✔</span>
                    Сделать выбранный рецептом запуска
                </button>
                <button class="secondary" id="btnOpenEditor">
                    <span class="icon">✎</span>
                    Открыть редактор рецептов
                </button>
            </div>

            <div id="routeMsg" class="msg"></div>
        </section>
    </div>

    <div class="footer">
        GalvaControl · ESP32 · магнитные метки · литий-полимерная гальваника
    </div>
</div>

<script>
const el = id => document.getElementById(id);
function setText(id, text) { el(id).textContent = text; }

function formatState(stateCode) {
    const map = {
        0: "IDLE",
        1: "HOMING",
        2: "MOVE_X",
        3: "LOWER_Z",
        4: "HOLD",
        5: "RAISE_Z",
        6: "DRY",
        7: "NEXT_STEP",
        8: "RETURN_HOME",
        9: "ERROR"
    };
    return map[stateCode] ?? ("#" + stateCode);
}
function showStatusMsg(ok, text) {
    setText("stLastStatus", text);
    const chip = el("stLastStatus").parentElement;
    chip.style.color = ok ? "#8fe7b7" : "#ffb4b4";
}

async function fetchStatus() {
    const nowStr = new Date().toLocaleTimeString();
    setText("stLastReq", nowStr);
    try {
        const res = await fetch("/status");
        if (!res.ok) {
            showStatusMsg(false, "HTTP " + res.status);
            return;
        }
        const data = await res.json();

        setText("stTime", data.time ?? "--:--:--");
        setText("stDate", data.date ?? "—");
        const stName = formatState(data.state);
        const stNode = el("stState");
        stNode.querySelector("span:nth-child(2)").textContent = stName;

        setText("stBath", data.bath ?? "–");
        const stepIdx = (data.step_idx == null || data.step_idx < 0) ? "–" : (data.step_idx + 1);
        setText("stStep", stepIdx);
        setText("stStepsTotal", "/ " + (data.steps ?? "–"));

        if (data.temp_c != null) setText("stTemp", data.temp_c.toFixed(1) + " °C");
        if (data.ph != null) setText("stPh", data.ph.toFixed(2));

        // Активный рецепт (берём из /status, он уже содержит active_route)
        if (data.active_route != null && data.active_route >= 0 && data.active_route !== 65535) {
            setText("activeRouteId", data.active_route);
            setText("activeRouteLabel", "Рецепт #" + data.active_route);
        } else {
            setText("activeRouteId", "–");
            setText("activeRouteLabel", "Не выбран");
        }

        showStatusMsg(true, "OK");
    } catch (e) {
        console.error(e);
        showStatusMsg(false, "Ошибка запроса");
    }
}

async function apiPost(path) {
    const res = await fetch(path, {method: "POST"});
    const txt = await res.text();
    if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);
    return txt;
}

async function handleStart() {
    el("btnStart").disabled = true;
    try {
        const txt = await apiPost("/start");
        showStatusMsg(true, "Старт: " + txt);
        await fetchStatus();
    } catch (e) {
        showStatusMsg(false, "Ошибка старта: " + e.message);
    } finally {
        el("btnStart").disabled = false;
    }
}
async function handleStop() {
    el("btnStop").disabled = true;
    try {
        const txt = await apiPost("/stop");
        showStatusMsg(true, "Стоп: " + txt);
        await fetchStatus();
    } catch (e) {
        showStatusMsg(false, "Ошибка стопа: " + e.message);
    } finally {
        el("btnStop").disabled = false;
    }
}

/* Работа с библиотекой рецептов на главной */
function setRouteMsg(ok, text) {
    const m = el("routeMsg");
    m.textContent = text || "";
    m.className = "msg " + (ok == null ? "" : ok ? "ok" : "err");
}

async function loadRoutesList() {
    try {
        const res = await fetch("/routes_list");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json(); // {active, routes:[{id,steps}]}

        const sel = el("routeSelect");
        sel.innerHTML = "";
        if (!data.routes || !data.routes.length) {
            const opt = document.createElement("option");
            opt.value = "";
            opt.textContent = "— рецептов нет —";
            sel.appendChild(opt);
        } else {
            const activeId = (data.active != null && data.active >= 0) ? data.active : -1;
            const placeholder = document.createElement("option");
            placeholder.value = "";
            placeholder.textContent = "Выберите рецепт…";
            sel.appendChild(placeholder);

            data.routes.forEach(r => {
                const opt = document.createElement("option");
                opt.value = r.id;
                opt.textContent = "ID " + r.id + " (" + r.steps + " шагов)";
                if (r.id === activeId) opt.textContent += " [активный]";
                sel.appendChild(opt);
            });
        }

        // Обновим активный ID из /routes_list, если он валиден
        if (data.active != null && data.active >= 0 && data.active !== 65535) {
            setText("activeRouteId", data.active);
            setText("activeRouteLabel", "Рецепт #" + data.active);
        }

        setRouteMsg(true, "Список рецептов обновлён");
    } catch (e) {
        console.error(e);
        setRouteMsg(false, "Ошибка загрузки списка: " + e.message);
    }
}

async function handleApplyRoute() {
    const sel = el("routeSelect");
    const id = sel.value;
    if (!id) {
        setRouteMsg(false, "Сначала выберите рецепт из списка");
        return;
    }
    setRouteMsg(null, "Применение рецепта #" + id + "…");
    try {
        const res = await fetch("/route_apply?id=" + encodeURIComponent(id), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);

        setRouteMsg(true, "Рецепт #" + id + " сделан активным");
        setText("activeRouteId", id);
        setText("activeRouteLabel", "Рецепт #" + id);

        // Обновим статус контроллера
        fetchStatus();
    } catch (e) {
        console.error(e);
        setRouteMsg(false, "Ошибка применения: " + e.message);
    }
}

document.addEventListener("DOMContentLoaded", () => {
    el("btnRefresh").addEventListener("click", () => fetchStatus());
    el("btnStart").addEventListener("click", handleStart);
    el("btnStop").addEventListener("click", handleStop);
    el("btnApplyRoute").addEventListener("click", handleApplyRoute);
    el("btnOpenEditor").addEventListener("click", () => {
        window.location.href = "/routes_ui";
    });

    fetchStatus();
    loadRoutesList();

    setInterval(fetchStatus, 3000);
});
</script>
</body>
</html>
)rawliteral";
