#include <Arduino.h>
#include "web_layout.h"
#include "web_logs.h"

const char WEB_LOGS_CONTENT[] PROGMEM = R"rawliteral(
<section class="card">
    <h2><span class="dot"></span> Архив действий</h2>
    <p style="font-size:.82rem;color:#8b93af;margin-top:0;">
        Архив хранится в браузере через IndexedDB. Контроллер отдает только короткий live-журнал,
        а браузер накапливает полную историю локально.
    </p>

    <div class="btn-row" style="margin-bottom:10px;">
        <button class="secondary" id="btnSyncLogs">Синхронизировать с контроллером</button>
        <button class="secondary" id="btnExportLogs">Экспорт CSV</button>
        <button class="danger" id="btnClearLogs">Очистить архив</button>
    </div>

    <div class="row" style="margin-bottom:10px;">
        <div class="pill-soft">Записей: <span id="logCount">0</span></div>
        <div class="pill-soft">Последняя синхронизация: <span id="lastSync">—</span></div>
    </div>

    <div style="overflow-x:auto;">
        <table id="logTable">
            <thead>
            <tr>
                <th>#</th>
                <th>Дата</th>
                <th>Время</th>
                <th>Уровень</th>
                <th>Категория</th>
                <th>Сообщение</th>
            </tr>
            </thead>
            <tbody></tbody>
        </table>
    </div>

    <div id="logsMsg" class="msg"></div>
</section>

<script>
const DB_NAME = "galvacontrol_logs";
const DB_VERSION = 1;
const STORE_NAME = "events";
const metaStore = "meta";

const el = id => document.getElementById(id);

function setMsg(ok, text) {
    const m = el("logsMsg");
    m.textContent = text || "";
    m.className = "msg " + (ok == null ? "" : ok ? "ok" : "err");
}

function openDb() {
    return new Promise((resolve, reject) => {
        const req = indexedDB.open(DB_NAME, DB_VERSION);
        req.onupgradeneeded = () => {
            const db = req.result;
            if (!db.objectStoreNames.contains(STORE_NAME)) {
                const store = db.createObjectStore(STORE_NAME, { keyPath: "seq" });
                store.createIndex("by_time", ["date", "time"], { unique: false });
            }
            if (!db.objectStoreNames.contains(metaStore)) {
                db.createObjectStore(metaStore, { keyPath: "key" });
            }
        };
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
    });
}

async function putLogs(items) {
    const db = await openDb();
    await new Promise((resolve, reject) => {
        const tx = db.transaction([STORE_NAME, metaStore], "readwrite");
        const store = tx.objectStore(STORE_NAME);
        items.forEach(item => store.put(item));
        tx.objectStore(metaStore).put({ key: "lastSync", value: new Date().toLocaleString() });
        tx.oncomplete = () => resolve();
        tx.onerror = () => reject(tx.error);
    });
    db.close();
}

async function readLogs() {
    const db = await openDb();
    const items = await new Promise((resolve, reject) => {
        const tx = db.transaction(STORE_NAME, "readonly");
        const req = tx.objectStore(STORE_NAME).getAll();
        req.onsuccess = () => resolve(req.result || []);
        req.onerror = () => reject(req.error);
    });
    db.close();
    items.sort((a, b) => (b.seq || 0) - (a.seq || 0));
    return items;
}

async function readLastSync() {
    const db = await openDb();
    const value = await new Promise((resolve, reject) => {
        const tx = db.transaction(metaStore, "readonly");
        const req = tx.objectStore(metaStore).get("lastSync");
        req.onsuccess = () => resolve(req.result ? req.result.value : "—");
        req.onerror = () => reject(req.error);
    });
    db.close();
    return value;
}

async function clearLogs() {
    const db = await openDb();
    await new Promise((resolve, reject) => {
        const tx = db.transaction([STORE_NAME, metaStore], "readwrite");
        tx.objectStore(STORE_NAME).clear();
        tx.objectStore(metaStore).put({ key: "lastSync", value: "—" });
        tx.oncomplete = () => resolve();
        tx.onerror = () => reject(tx.error);
    });
    db.close();
}

async function syncLogs() {
    setMsg(null, "Синхронизация журнала...");
    try {
        const res = await fetch("/action_log");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();
        const items = Array.isArray(data.items) ? data.items : [];
        await putLogs(items);
        await renderLogs();
        setMsg(true, "Журнал синхронизирован");
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка синхронизации: " + e.message);
    }
}

async function renderLogs() {
    const tbody = el("logTable").querySelector("tbody");
    tbody.innerHTML = "";
    const items = await readLogs();
    el("logCount").textContent = String(items.length);
    el("lastSync").textContent = await readLastSync();

    if (!items.length) {
        const tr = document.createElement("tr");
        tr.innerHTML = `<td colspan="6">Архив пуст</td>`;
        tbody.appendChild(tr);
        return;
    }

    items.forEach(item => {
        const tr = document.createElement("tr");
        tr.innerHTML = `
            <td>${item.seq ?? ""}</td>
            <td>${item.date ?? ""}</td>
            <td>${item.time ?? ""}</td>
            <td>${item.level ?? ""}</td>
            <td>${item.category ?? ""}</td>
            <td style="text-align:left">${item.message ?? ""}</td>
        `;
        tbody.appendChild(tr);
    });
}

async function exportCsv() {
    const items = await readLogs();
    let csv = "seq,date,time,level,category,message\r\n";
    items.slice().reverse().forEach(item => {
        const safe = v => `"${String(v ?? "").replace(/"/g, '""')}"`;
        csv += [
            item.seq ?? "",
            safe(item.date),
            safe(item.time),
            safe(item.level),
            safe(item.category),
            safe(item.message)
        ].join(",") + "\r\n";
    });

    const blob = new Blob([csv], { type: "text/csv;charset=utf-8;" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "galvacontrol-log-archive.csv";
    a.click();
    URL.revokeObjectURL(url);
}

document.addEventListener("DOMContentLoaded", async () => {
    el("btnSyncLogs").addEventListener("click", syncLogs);
    el("btnExportLogs").addEventListener("click", exportCsv);
    el("btnClearLogs").addEventListener("click", async () => {
        if (!confirm("Очистить архив журнала в браузере?")) return;
        await clearLogs();
        await renderLogs();
        setMsg(true, "Архив очищен");
    });

    await renderLogs();
    await syncLogs();
});
</script>
)rawliteral";

String renderPageLogs() {
    String html = FPSTR(WEB_LAYOUT);

    html.replace("{{title}}", "Архив журнала");
    html.replace("{{nav_logs}}", "active");
    html.replace("{{nav_index}}", "");
    html.replace("{{nav_monitor}}", "");
    html.replace("{{nav_routes}}", "");
    html.replace("{{nav_baths}}", "");
    html.replace("{{nav_autolearn}}", "");
    html.replace("{{content}}", FPSTR(WEB_LOGS_CONTENT));

    return html;
}
