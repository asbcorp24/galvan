#include <Arduino.h>
#include "web_layout.h"
#include "web_baths.h"

const char WEB_BATHS_CONTENT[] PROGMEM = R"rawliteral(
<section class="card">
        <h2><span class="dot"></span> Ванны и их RFID-метки</h2>
        <p style="font-size:.8rem;color:#8b93af;margin-top:0;">
            Нажмите «Считать метку» → поднесите карту/метку к считывателю PN532. UID запишется в выбранную ванну.
        </p>

        <div style="overflow-x:auto;">
            <table id="bathsTable">
                <thead>
                <tr>
                    <th>#</th>
                    <th>Логический номер</th>
                    <th>UID RFID</th>
                    <th>Старт</th>
                    <th>Финиш</th>
                    <th>Действия</th>
                </tr>
                </thead>
                <tbody></tbody>
            </table>
        </div>

        <div style="margin-top:8px;display:flex;flex-wrap:wrap;gap:8px;">
            <button class="secondary" id="btnReload">⟳ Обновить список</button>
            <button class="primary" id="btnSave">💾 Сохранить флаги</button>
        </div>

        <div id="bathMsg" class="msg"></div>
    </section>

    <div class="footer">
        GalvaControl · RFID → ванна · ESP32 + PN532
    </div>
</div>

<script>
const el = id => document.getElementById(id);

function setMsg(ok, text) {
    const m = el("bathMsg");
    m.textContent = text || "";
    m.className = "msg " + (ok == null ? "" : ok ? "ok" : "err");
}

function uidToLabel(uidHex) {
    if (!uidHex) return "—";
    return uidHex.replace(/(.{2})/g, "$1 ").trim();
}

async function loadBaths() {
    setMsg(null, "Загрузка списка ванн...");
    const tbody = el("bathsTable").querySelector("tbody");
    tbody.innerHTML = "";
    try {
        const res = await fetch("/baths_list");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();

        if (!data.baths || !data.baths.length) {
            const tr = document.createElement("tr");
            const td = document.createElement("td");
            td.colSpan = 6;
            td.textContent = "Ванны не инициализированы";
            tr.appendChild(td);
            tbody.appendChild(tr);
        } else {
            data.baths.forEach((b, idx) => {
                const tr = document.createElement("tr");
                tr.dataset.index = b.index;

                tr.innerHTML = `
                    <td>${idx+1}</td>
                    <td>
                        <input type="number" min="0" max="65535" value="${b.bathNumber}">
                    </td>
                    <td class="uid-cell">${uidToLabel(b.uid_hex)}</td>
                    <td><input type="checkbox" class="chk-start" ${b.isStart ? "checked" : ""}></td>
                    <td><input type="checkbox" class="chk-end" ${b.isEnd ? "checked" : ""}></td>
                    <td>
                        <button class="secondary btn-learn">📡 Считать метку</button>
                    </td>
                `;
                tbody.appendChild(tr);
            });
        }

        setMsg(true, "Список ванн загружен");
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка загрузки: " + e.message);
    }
}

async function learnTagForRow(tr) {
    const idx = parseInt(tr.dataset.index, 10);
    if (isNaN(idx)) return;
    setMsg(null, "Считывание метки для ванны #" + idx + "... Поднесите карту к PN532.");

    try {
        const res = await fetch("/baths_learn?index=" + encodeURIComponent(idx), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));

        const data = JSON.parse(txt);
        if (!data.ok) throw new Error(data.error || "Неизвестная ошибка");

        const cell = tr.querySelector(".uid-cell");
        cell.textContent = uidToLabel(data.uid_hex);
        setMsg(true, "UID записан для ванны #" + idx);
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка считывания: " + e.message);
    }
}

async function saveBathFlags() {
    const rows = Array.from(el("bathsTable").querySelectorAll("tbody tr"));
    if (!rows.length) return;

    const payload = { baths: [] };
    rows.forEach(tr => {
        const idx = parseInt(tr.dataset.index, 10);
        if (isNaN(idx)) return;
        const num = tr.querySelector("input[type='number']").value;
        const isStart = tr.querySelector(".chk-start").checked;
        const isEnd = tr.querySelector(".chk-end").checked;
        payload.baths.push({
            index: idx,
            bathNumber: parseInt(num || "0", 10),
            isStart: isStart,
            isEnd: isEnd
        });
    });

    setMsg(null, "Сохранение параметров ванн...");
    try {
        const res = await fetch("/baths_update", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(payload)
        });
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        setMsg(true, "Параметры ванн сохранены");
        loadBaths();
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка сохранения: " + e.message);
    }
}

document.addEventListener("DOMContentLoaded", () => {
    el("btnReload").addEventListener("click", loadBaths);
    el("btnSave").addEventListener("click", saveBathFlags);

    el("bathsTable").addEventListener("click", (e) => {
        const btn = e.target.closest(".btn-learn");
        if (!btn) return;
        const tr = btn.closest("tr");
        if (!tr) return;
        learnTagForRow(tr);
    });

    loadBaths();
});
</script>
)rawliteral";


String renderPageBaths() {
    String html = FPSTR(WEB_LAYOUT);

    html.replace("{{title}}", "Привязка RFID");
    html.replace("{{nav_baths}}", "active");

    html.replace("{{nav_index}}", "");
    html.replace("{{nav_monitor}}", "");
    html.replace("{{nav_routes}}", "");
    html.replace("{{nav_autolearn}}", "");

    html.replace("{{content}}", FPSTR(WEB_BATHS_CONTENT));

    return html;
}
