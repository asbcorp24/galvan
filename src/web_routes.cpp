#include <Arduino.h>
#include "web_layout.h"
#include "web_routes.h"

const char WEB_ROUTES_CONTENT[] PROGMEM = R"rawliteral(
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
                Структура шага: ванна → Z↓ уровень + таймаут → выдержка → Z↑ уровень + таймаут → сушка + флаги Spin/Fan.
            </div>

            <div style="margin-top:8px; overflow-x:auto;">
                <table id="stepsTable">
                    <thead>
                    <tr>
                        <th>#</th>
                        <th>Ванна</th>
                        <th>Z↓ ур.</th>
                        <th>Z↓ тайм., c</th>
                        <th>Выдержка, c</th>
                        <th>Z↑ ур.</th>
                        <th>Z↑ тайм., c</th>
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
        z_level_down: 1,
        z_down_timeout_s: 3,
        hold_s: 60,
        z_level_up: 0,
        z_up_timeout_s: 3,
        dry_s: 10,
        spin: false,
        fan: false
    };
    const normalized = Object.assign({}, data || {});
    if (normalized.z_down_timeout_s == null && normalized.z_down_s != null) {
        normalized.z_down_timeout_s = normalized.z_down_s;
    }
    if (normalized.z_up_timeout_s == null && normalized.z_up_s != null) {
        normalized.z_up_timeout_s = normalized.z_up_s;
    }
    const s = Object.assign({}, defaults, normalized);

    const tr = document.createElement("tr");
    tr.innerHTML = `
        <td class="col-idx"></td>
        <td><input type="number" min="0" max="65535" value="${s.bath}"></td>
        <td><input type="number" min="0" max="65535" value="${s.z_level_down}"></td>
        <td><input type="number" min="0" max="65535" value="${s.z_down_timeout_s}"></td>
        <td><input type="number" min="0" max="65535" value="${s.hold_s}"></td>
        <td><input type="number" min="0" max="65535" value="${s.z_level_up}"></td>
        <td><input type="number" min="0" max="65535" value="${s.z_up_timeout_s}"></td>
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
        const zDownLevel = num(2);
        const zDownTimeout = num(3);
        const hold   = num(4);
        const zUpLevel = num(5);
        const zUpTimeout = num(6);
        const dry    = num(7);
        const spinOn = cells[8].querySelector("input").checked;
        const fanOn  = cells[9].querySelector("input").checked;
        steps.push({
            bath: bath,
            z_level_down: zDownLevel,
            z_down_timeout_s: zDownTimeout,
            hold_s: hold,
            z_level_up: zUpLevel,
            z_up_timeout_s: zUpTimeout,
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
    createStepRow({bath: 1, z_level_down: 1, z_down_timeout_s: 3, hold_s: 60, z_level_up: 0, z_up_timeout_s: 3});
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
)rawliteral";


String renderPageRoutes() {
    String html = FPSTR(WEB_LAYOUT);

    html.replace("{{title}}", "Редактор рецептов");
    html.replace("{{nav_routes}}", "active");

    html.replace("{{nav_index}}", "");
    html.replace("{{nav_monitor}}", "");
    html.replace("{{nav_baths}}", "");
    html.replace("{{nav_autolearn}}", "");
    html.replace("{{nav_logs}}", "");

    html.replace("{{content}}", FPSTR(WEB_ROUTES_CONTENT));

    return html;
}
