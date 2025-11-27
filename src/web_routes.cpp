#include "web_routes.h"

const char WEB_UI_ROUTES[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>GalvaControl — Редактор рецептов</title>
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
            font-size: 1.3rem;
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
            grid-template-columns: minmax(0, 1.1fr) minmax(0, 1.7fr);
            gap: 16px;
        }
        @media (max-width: 1000px) {
            .layout { grid-template-columns: 1fr; }
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
        .label {
            font-size: .7rem;
            text-transform: uppercase;
            letter-spacing: .08em;
            color: var(--muted);
            margin-bottom: 2px;
        }
        .value {
            font-size: .9rem;
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

        table {
            width: 100%;
            border-collapse: collapse;
            font-size: .8rem;
            margin-top: 4px;
        }
        thead { background: rgba(10,16,35,0.9); }
        th, td {
            padding: 4px 5px;
            border-bottom: 1px solid rgba(255,255,255,0.03);
            text-align: center;
        }
        th {
            font-weight: 500;
            color: var(--muted);
            text-transform: uppercase;
            font-size: .7rem;
        }
        tbody tr:nth-child(even) { background: rgba(255,255,255,0.01); }
        tbody tr:hover { background: rgba(59,167,255,0.05); }
        td input[type="number"] { width: 60px; }
        td input[type="checkbox"] { transform: scale(1.1); }

        input[type="number"], input[type="text"] {
            background: rgba(3,8,20,0.9);
            border-radius: 999px;
            border: 1px solid var(--border);
            padding: 5px 8px;
            color: var(--text);
            font-size: .8rem;
            outline: none;
            min-width: 0;
        }
        input[type="number"]:focus, input[type="text"]:focus {
            border-color: var(--accent);
            box-shadow: 0 0 0 1px rgba(59,167,255,0.3);
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
        .table-actions button {
            font-size: .7rem;
            padding: 4px 8px;
        }
    </style>
</head>
<body>
<div class="page">
    <header>
        <div>
            <h1><span>Galva</span>Control</h1>
            <div class="subtitle">Редактор рецептов / паттернов движения крана</div>
        </div>
        <div class="pill">
            Режим: промышленный табличный редактор
        </div>
    </header>

    <nav class="nav-bar">
        <a href="/" class="nav-link"><span class="icon">⚙</span>Панель управления</a>
        <a href="/monitor" class="nav-link"><span class="icon">📈</span>Монитор ванн</a>
        <a href="/routes_ui" class="nav-link active"><span class="icon">📋</span>Рецепты / паттерны</a>
    </nav>

    <div class="layout">
        <!-- Левая карта: список рецептов -->
        <section class="card">
            <h2><span class="dot"></span> Библиотека рецептов (NVS)</h2>

            <div class="label">Активный рецепт</div>
            <div class="value">
                <span class="pill-soft">
                    ID активного: <span id="activeRouteId">–</span>
                </span>
            </div>

            <div class="btn-row" style="margin-top:8px;">
                <button class="secondary" id="btnReloadList">
                    <span class="icon">⟳</span>Обновить список
                </button>
                <button class="secondary" id="btnNewRoute">
                    <span class="icon">＋</span>Создать новый рецепт по ID
                </button>
            </div>

            <div style="margin-top:8px; overflow-x:auto;">
                <table id="routesTable">
                    <thead>
                    <tr>
                        <th>ID</th>
                        <th>Шагов</th>
                        <th>Действия</th>
                    </tr>
                    </thead>
                    <tbody></tbody>
                </table>
            </div>

            <div id="routesMsg" class="msg"></div>
        </section>

        <!-- Правая карта: редактор выбранного рецепта -->
        <section class="card">
            <h2><span class="dot"></span> Редактор рецепта</h2>

            <div class="row">
                <div>
                    <div class="label">Текущий ID рецепта</div>
                    <div class="value">
                        <span class="pill-soft">
                            ID: <span id="editRouteId">—</span>
                        </span>
                    </div>
                </div>
                <div>
                    <div class="label">Шагов в текущем рецепте</div>
                    <div class="value">
                        <span class="pill-soft">
                            <span id="editStepsCount">0</span> шаг(ов)
                        </span>
                    </div>
                </div>
            </div>

            <div class="label" style="margin-top:6px;">
                Структура шага: ванна → опускание (Z↓) → выдержка → подъём (Z↑) → сушка + флаги Spin/Fan.
            </div>

            <div style="margin-top:8px; overflow-x:auto;">
                <table id="stepsTable">
                    <thead>
                    <tr>
                        <th>#</th>
                        <th>Ванна</th>
                        <th>Z↓, c</th>
                        <th>Выдержка, c</th>
                        <th>Z↑, c</th>
                        <th>Сушка, c</th>
                        <th>Spin</th>
                        <th>Fan</th>
                        <th>×</th>
                    </tr>
                    </thead>
                    <tbody></tbody>
                </table>
            </div>

            <div class="btn-row" style="margin-top:8px;">
                <button class="secondary" id="btnAddStep">
                    <span class="icon">＋</span>Добавить шаг
                </button>
                <button class="primary" id="btnSaveRoute">
                    <span class="icon">💾</span>Сохранить в NVS
                </button>
                <button class="secondary" id="btnApplyFromEditor">
                    <span class="icon">✔</span>Сделать активным
                </button>
            </div>

            <div class="btn-row" style="margin-top:6px;">
                <button class="secondary" id="btnExportJson">
                    <span class="icon">⬇</span>Экспорт в JSON
                </button>
                <button class="secondary" id="btnImportJson">
                    <span class="icon">⬆</span>Импорт из JSON
                </button>
                <input type="file" id="fileInput" accept="application/json" style="display:none;">
            </div>

            <div id="editorMsg" class="msg"></div>
        </section>
    </div>

    <div class="footer">
        GalvaControl · ESP32 · NVS библиотека рецептов · экспорт / импорт JSON
    </div>
</div>

<script>
const el = id => document.getElementById(id);
function setText(id, txt) { el(id).textContent = txt; }

let currentRouteId = null;

function setRoutesMsg(ok, text) {
    const m = el("routesMsg");
    m.textContent = text || "";
    m.className = "msg " + (ok == null ? "" : ok ? "ok" : "err");
}
function setEditorMsg(ok, text) {
    const m = el("editorMsg");
    m.textContent = text || "";
    m.className = "msg " + (ok == null ? "" : ok ? "ok" : "err");
}

/* ---------- Работа со списком рецептов ---------- */

async function loadRoutesList() {
    setRoutesMsg(null, "Загрузка списка рецептов...");
    const tbody = el("routesTable").querySelector("tbody");
    tbody.innerHTML = "";
    try {
        const res = await fetch("/routes_list");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json(); // {active, routes:[{id,steps}]}

        const active = (data.active != null && data.active >= 0 && data.active !== 65535)
            ? data.active
            : null;
        setText("activeRouteId", active != null ? active : "—");

        if (!data.routes || !data.routes.length) {
            const tr = document.createElement("tr");
            const td = document.createElement("td");
            td.colSpan = 3;
            td.textContent = "Рецептов пока нет";
            td.style.textAlign = "center";
            tr.appendChild(td);
            tbody.appendChild(tr);
        } else {
            data.routes.forEach(r => {
                const tr = document.createElement("tr");

                const tdId = document.createElement("td");
                tdId.textContent = r.id;
                if (active != null && r.id === active) {
                    tdId.textContent += " ★";
                }
                tr.appendChild(tdId);

                const tdSteps = document.createElement("td");
                tdSteps.textContent = r.steps;
                tr.appendChild(tdSteps);

                const tdActions = document.createElement("td");
                tdActions.className = "table-actions";
                tdActions.innerHTML = `
                    <button class="secondary btn-edit" data-id="${r.id}">✎</button>
                    <button class="secondary btn-apply" data-id="${r.id}">✔</button>
                    <button class="secondary btn-export" data-id="${r.id}">⬇</button>
                    <button class="danger btn-delete" data-id="${r.id}">✖</button>
                `;
                tr.appendChild(tdActions);

                tbody.appendChild(tr);
            });
        }

        setRoutesMsg(true, "Список рецептов загружен");
    } catch (e) {
        console.error(e);
        setRoutesMsg(false, "Ошибка загрузки списка: " + e.message);
    }
}

/* ---------- Редактор шагов ---------- */

const stepsTbody = () => el("stepsTable").querySelector("tbody");

function renumberSteps() {
    const rows = [...stepsTbody().querySelectorAll("tr")];
    rows.forEach((tr, idx) => {
        const cell = tr.querySelector(".col-idx");
        if (cell) cell.textContent = idx + 1;
    });
    setText("editStepsCount", rows.length);
}

function createStepRow(data) {
    const defaults = {
        bath: 1,
        z_down_s: 3,
        hold_s: 60,
        z_up_s: 3,
        dry_s: 10,
        spin: false,
        fan: false
    };
    const s = Object.assign({}, defaults, data || {});

    const tr = document.createElement("tr");
    tr.innerHTML = `
        <td class="col-idx"></td>
        <td><input type="number" min="0" max="65535" value="${s.bath}"></td>
        <td><input type="number" min="0" max="65535" value="${s.z_down_s}"></td>
        <td><input type="number" min="0" max="65535" value="${s.hold_s}"></td>
        <td><input type="number" min="0" max="65535" value="${s.z_up_s}"></td>
        <td><input type="number" min="0" max="65535" value="${s.dry_s}"></td>
        <td><input type="checkbox" ${s.spin ? "checked" : ""}></td>
        <td><input type="checkbox" ${s.fan ? "checked" : ""}></td>
        <td><button class="secondary btn-del">×</button></td>
    `;
    stepsTbody().appendChild(tr);
    renumberSteps();
}

function clearStepsTable() {
    stepsTbody().innerHTML = "";
    renumberSteps();
}

function getStepsFromTable() {
    const steps = [];
    const rows = [...stepsTbody().querySelectorAll("tr")];
    rows.forEach(tr => {
        const cells = tr.querySelectorAll("td");
        const num = idx => parseInt(cells[idx].querySelector("input").value || "0", 10);
        const bath   = num(1);
        const zDown  = num(2);
        const hold   = num(3);
        const zUp    = num(4);
        const dry    = num(5);
        const spinOn = cells[6].querySelector("input").checked;
        const fanOn  = cells[7].querySelector("input").checked;
        steps.push({
            bath: bath,
            z_down_s: zDown,
            hold_s: hold,
            z_up_s: zUp,
            dry_s: dry,
            spin: spinOn,
            fan: fanOn
        });
    });
    return steps;
}

function openEditorForId(id, steps) {
    currentRouteId = id;
    setText("editRouteId", id);
    clearStepsTable();
    steps.forEach(s => createStepRow(s));
    setEditorMsg(true, "Рецепт #" + id + " загружен в редактор");
}

/* ---------- API для одного рецепта ---------- */

async function loadRouteById(id) {
    setEditorMsg(null, "Загрузка рецепта #" + id + "…");
    try {
        const res = await fetch("/route_get?id=" + encodeURIComponent(id));
        const txt = await res.text();
        if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);

        const data = JSON.parse(txt); // {id, steps:[...]}
        if (!data.steps || !Array.isArray(data.steps)) {
            throw new Error("Некорректный формат ответа");
        }
        openEditorForId(data.id ?? id, data.steps);
    } catch (e) {
        console.error(e);
        setEditorMsg(false, "Ошибка загрузки: " + e.message);
    }
}

async function saveCurrentRoute() {
    if (currentRouteId == null) {
        setEditorMsg(false, "Сначала выберите ID рецепта (редактируете ничего)");
        return;
    }
    const steps = getStepsFromTable();
    if (!steps.length) {
        setEditorMsg(false, "Добавьте хотя бы один шаг");
        return;
    }
    setEditorMsg(null, "Сохранение рецепта #" + currentRouteId + "…");
    try {
        const res = await fetch("/route_save?id=" + encodeURIComponent(currentRouteId), {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(steps)
        });
        const txt = await res.text();
        if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);

        setEditorMsg(true, "Рецепт #" + currentRouteId + " сохранён в NVS");
        loadRoutesList();
    } catch (e) {
        console.error(e);
        setEditorMsg(false, "Ошибка сохранения: " + e.message);
    }
}

async function applyCurrentRoute() {
    if (currentRouteId == null) {
        setEditorMsg(false, "Нет выбранного рецепта для активации");
        return;
    }
    setEditorMsg(null, "Активация рецепта #" + currentRouteId + "…");
    try {
        const res = await fetch("/route_apply?id=" + encodeURIComponent(currentRouteId), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);
        setEditorMsg(true, "Рецепт #" + currentRouteId + " сделан активным");
        setText("activeRouteId", currentRouteId);
        loadRoutesList();
    } catch (e) {
        console.error(e);
        setEditorMsg(false, "Ошибка активации: " + e.message);
    }
}

async function deleteRoute(id) {
    if (!confirm("Удалить рецепт #" + id + " из NVS?")) return;
    setRoutesMsg(null, "Удаление рецепта #" + id + "…");
    try {
        const res = await fetch("/route_delete?id=" + encodeURIComponent(id), {method: "DELETE"});
        const txt = await res.text();
        if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);
        setRoutesMsg(true, "Рецепт #" + id + " удалён");
        if (currentRouteId == id) {
            currentRouteId = null;
            setText("editRouteId", "—");
            clearStepsTable();
        }
        loadRoutesList();
    } catch (e) {
        console.error(e);
        setRoutesMsg(false, "Ошибка удаления: " + e.message);
    }
}

/* ---------- Экспорт / импорт JSON ---------- */

async function exportRouteJson(id) {
    try {
        const res = await fetch("/route_get?id=" + encodeURIComponent(id));
        const txt = await res.text();
        if (!res.ok) throw new Error("HTTP " + res.status + " " + txt);

        const blob = new Blob([txt], {type: "application/json"});
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = "route_" + id + ".json";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(a.href);

        setRoutesMsg(true, "Рецепт #" + id + " экспортирован в JSON");
    } catch (e) {
        console.error(e);
        setRoutesMsg(false, "Ошибка экспорта: " + e.message);
    }
}

function handleImportClick() {
    el("fileInput").value = "";
    el("fileInput").click();
}

function handleFileSelected(ev) {
    const file = ev.target.files && ev.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
        try {
            const text = reader.result;
            const data = JSON.parse(text);

            let id = currentRouteId;
            let steps = null;

            if (Array.isArray(data)) {
                steps = data;
            } else if (data && Array.isArray(data.steps)) {
                steps = data.steps;
                if (typeof data.id === "number") {
                    if (confirm("В файле указан ID " + data.id + ". Использовать его?")) {
                        id = data.id;
                    }
                }
            } else {
                throw new Error("Некорректный JSON: нет массива шагов");
            }

            if (id == null) {
                const answer = prompt("Укажите ID рецепта, в который импортировать:", "1");
                if (!answer) return;
                id = parseInt(answer, 10);
                if (isNaN(id)) throw new Error("Некорректный ID");
            }

            openEditorForId(id, steps);
            setEditorMsg(true, "Импортировано " + steps.length + " шагов в рецепт #" + id);
        } catch (e) {
            console.error(e);
            setEditorMsg(false, "Ошибка импорта: " + e.message);
        }
    };
    reader.onerror = () => {
        setEditorMsg(false, "Ошибка чтения файла");
    };
    reader.readAsText(file, "utf-8");
}

/* ---------- Создание нового рецепта ---------- */

function createNewRoute() {
    const answer = prompt("Введите числовой ID нового рецепта:", "");
    if (!answer) return;
    const id = parseInt(answer, 10);
    if (isNaN(id)) {
        alert("ID должен быть числом");
        return;
    }
    currentRouteId = id;
    setText("editRouteId", id);
    clearStepsTable();
    createStepRow({bath: 1, hold_s: 60});
    setEditorMsg(true, "Создан новый рецепт #" + id + " (пока только в редакторе, сохраните его)");
}

/* ---------- Инициализация ---------- */

document.addEventListener("DOMContentLoaded", () => {
    el("btnReloadList").addEventListener("click", loadRoutesList);
    el("btnNewRoute").addEventListener("click", createNewRoute);

    el("btnAddStep").addEventListener("click", () => createStepRow());
    el("btnSaveRoute").addEventListener("click", saveCurrentRoute);
    el("btnApplyFromEditor").addEventListener("click", applyCurrentRoute);
    el("btnExportJson").addEventListener("click", () => {
        if (currentRouteId == null) {
            setEditorMsg(false, "Нет выбранного рецепта для экспорта");
            return;
        }
        exportRouteJson(currentRouteId);
    });
    el("btnImportJson").addEventListener("click", handleImportClick);
    el("fileInput").addEventListener("change", handleFileSelected);

    // Делегирование кнопок в таблице рецептов
    el("routesTable").addEventListener("click", (e) => {
        const btn = e.target.closest("button");
        if (!btn) return;
        const id = parseInt(btn.getAttribute("data-id"), 10);
        if (isNaN(id)) return;

        if (btn.classList.contains("btn-edit")) {
            loadRouteById(id);
        } else if (btn.classList.contains("btn-apply")) {
            currentRouteId = id;
            applyCurrentRoute();
        } else if (btn.classList.contains("btn-export")) {
            exportRouteJson(id);
        } else if (btn.classList.contains("btn-delete")) {
            deleteRoute(id);
        }
    });

    // Удаление шага
    stepsTbody().addEventListener("click", (e) => {
        if (e.target.classList.contains("btn-del")) {
            const tr = e.target.closest("tr");
            if (tr) {
                tr.parentNode.removeChild(tr);
                renumberSteps();
            }
        }
    });

    // стартовая загрузка
    loadRoutesList();
});
</script>
</body>
</html>
)rawliteral";
