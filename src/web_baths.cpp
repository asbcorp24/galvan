#include <Arduino.h>
#include "web_layout.h"
#include "web_baths.h"

const char WEB_BATHS_CONTENT[] PROGMEM = R"rawliteral(
<section class="card">
    <h2><span class="dot"></span> Рабочие ванны и RFID</h2>
    <p style="font-size:.8rem;color:#8b93af;margin-top:0;">
        Здесь задаются только рабочие ванны для рецептов. Служебные точки линии <b>Start</b> и <b>End</b>
        обучаются отдельно, а RFID уровни Z используются для вертикального позиционирования подъёмника.
    </p>

    <div class="row" style="margin-bottom:10px;">
        <div class="pill-soft">Start: <span id="startUidLabel">—</span></div>
        <div class="pill-soft">End: <span id="endUidLabel">—</span></div>
        <div class="pill-soft">Z Start: <span id="zStartUidLabel">—</span></div>
        <div class="pill-soft">Z End: <span id="zEndUidLabel">—</span></div>
        <div class="pill-soft">Текущий Z: <span id="currentZLabel">—</span></div>
    </div>

    <div class="btn-row" style="margin-bottom:12px;">
        <button class="secondary" id="btnLearnStart">📍 Считать Start</button>
        <button class="secondary" id="btnLearnEnd">🏁 Считать End</button>
        <button class="secondary" id="btnLearnZStart">⬆ Считать Z Start</button>
        <button class="secondary" id="btnLearnZEnd">⬇ Считать Z End</button>
    </div>

    <div style="overflow-x:auto;">
        <table id="bathsTable">
            <thead>
            <tr>
                <th>#</th>
                <th>Логический номер</th>
                <th>UID RFID</th>
                <th>Действия</th>
            </tr>
            </thead>
            <tbody></tbody>
        </table>
    </div>

    <div class="btn-row" style="margin-top:8px;">
        <button class="secondary" id="btnAddBath">+ Add bath</button>
        <button class="secondary" id="btnReload">🟳 Обновить список</button>
        <button class="primary" id="btnSave">💾 Сохранить ванны</button>
    </div>

    <h3 style="margin-top:18px;">RFID уровни Z</h3>
    <div class="row">
        <div>
            <div class="label">Номер уровня</div>
            <input type="number" id="zLevelInput" min="0" max="32767" value="0">
        </div>
        <div style="display:flex;align-items:flex-end;">
            <button class="secondary" id="btnLearnZ">↕ Считать Z-метку</button>
            <button class="secondary" id="btnAutoZ">Auto Z</button>
        </div>
    </div>

    <div style="overflow-x:auto; margin-top:8px;">
        <table id="zTagsTable">
            <thead>
            <tr>
                <th>Уровень</th>
                <th>UID RFID</th>
                <th>Actions</th>
            </tr>
            </thead>
            <tbody></tbody>
        </table>
    </div>

    <div id="bathMsg" class="msg"></div>
</section>

<div class="footer">
    GalvaControl · RFID line points + process baths + Z levels
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
    setMsg(null, "Загрузка конфигурации RFID...");
    const tbody = el("bathsTable").querySelector("tbody");
    const zTbody = el("zTagsTable").querySelector("tbody");
    tbody.innerHTML = "";
    zTbody.innerHTML = "";

    try {
        const res = await fetch("/baths_list");
        if (!res.ok) throw new Error("HTTP " + res.status);
        const data = await res.json();

        el("startUidLabel").textContent = uidToLabel(data.service_points?.start?.uid_hex);
        el("endUidLabel").textContent = uidToLabel(data.service_points?.end?.uid_hex);
        el("zStartUidLabel").textContent = uidToLabel(data.service_points?.z_start?.uid_hex);
        el("zEndUidLabel").textContent = uidToLabel(data.service_points?.z_end?.uid_hex);
        if (el("currentZLabel")) {
            const currentZ = (data.current_z_level ?? data.z_level);
            el("currentZLabel").textContent = currentZ == null || currentZ === "" || currentZ < 0 ? "—" : String(currentZ);
        }

        if (!data.baths || !data.baths.length) {
            const tr = document.createElement("tr");
            tr.innerHTML = `<td colspan="4">Рабочие ванны пока не заданы</td>`;
            tbody.appendChild(tr);
        } else {
            data.baths.forEach((b, idx) => {
                const tr = document.createElement("tr");
                tr.dataset.index = b.index;
                tr.innerHTML = `
                    <td>${idx + 1}</td>
                    <td><input type="number" min="0" max="65535" value="${b.bathNumber}"></td>
                    <td class="uid-cell">${uidToLabel(b.uid_hex)}</td>
                    <td><button class="secondary btn-learn">Scan tag</button> <button class="secondary btn-delete">Delete</button></td>
                `;
                tbody.appendChild(tr);
            });
        }

        if (!data.z_tags || !data.z_tags.length) {
            const tr = document.createElement("tr");
            tr.innerHTML = `<td colspan="2">Z-уровни ещё не обучены</td>`;
            zTbody.appendChild(tr);
        } else {
            data.z_tags.forEach(z => {
                const tr = document.createElement("tr");
                tr.dataset.level = z.level;
                tr.innerHTML = `
                    <td>${z.level}</td>
                    <td>${uidToLabel(z.uid_hex)}</td>
                    <td><button class="secondary btn-z-relearn">Relearn</button> <button class="secondary btn-z-delete">Delete</button></td>
                `;
                zTbody.appendChild(tr);
            });
        }

        setMsg(true, "Конфигурация RFID загружена");
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка загрузки: " + e.message);
    }
}

async function learnTagForRow(tr) {
    const idx = parseInt(tr.dataset.index, 10);
    if (isNaN(idx)) return;
    setMsg(null, "Считывание метки для рабочей ванны #" + idx + "...");

    try {
        const res = await fetch("/baths_learn?index=" + encodeURIComponent(idx), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));

        const data = JSON.parse(txt);
        if (!data.ok) throw new Error(data.error || "Неизвестная ошибка");

        tr.querySelector(".uid-cell").textContent = uidToLabel(data.uid_hex);
        setMsg(true, "UID записан для рабочей ванны #" + idx);
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка считывания: " + e.message);
    }
}

async function addBath() {
    setMsg(null, "Adding bath...");
    try {
        const res = await fetch("/baths_add", {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        setMsg(true, "Bath added");
        await loadBaths();
    } catch (e) {
        console.error(e);
        setMsg(false, "Add error: " + e.message);
    }
}

async function deleteBath(tr) {
    const idx = parseInt(tr.dataset.index, 10);
    if (isNaN(idx)) return;
    if (!confirm("Delete bath from list?")) return;

    setMsg(null, "Deleting bath...");
    try {
        const res = await fetch("/baths_delete?index=" + encodeURIComponent(idx), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        setMsg(true, "Bath deleted");
        await loadBaths();
    } catch (e) {
        console.error(e);
        setMsg(false, "Delete error: " + e.message);
    }
}

async function saveBaths() {
    const rows = Array.from(el("bathsTable").querySelectorAll("tbody tr"));
    const payload = { baths: [] };

    rows.forEach(tr => {
        const idx = parseInt(tr.dataset.index, 10);
        if (isNaN(idx)) return;
        const num = tr.querySelector("input[type='number']").value;
        payload.baths.push({
            index: idx,
            bathNumber: parseInt(num || "0", 10)
        });
    });

    setMsg(null, "Сохранение рабочих ванн...");
    try {
        const res = await fetch("/baths_update", {
            method: "POST",
            headers: {"Content-Type": "application/json"},
            body: JSON.stringify(payload)
        });
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        setMsg(true, "Рабочие ванны сохранены");
        loadBaths();
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка сохранения: " + e.message);
    }
}

async function learnServicePoint(kind) {
    setMsg(null, "Считывание " + kind + " метки...");
    try {
        const res = await fetch("/service_point_learn?kind=" + encodeURIComponent(kind), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        JSON.parse(txt);
        setMsg(true, "Служебная точка " + kind + " сохранена");
        await loadBaths();
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка обучения служебной точки: " + e.message);
    }
}

async function autoLearnZ() {
    const level = parseInt(el("zLevelInput").value || "0", 10);
    setMsg(null, "Auto learn Z from level " + level + "...");
    try {
        const res = await fetch("/z_autolearn?start=" + encodeURIComponent(level), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        const data = JSON.parse(txt);
        setMsg(true, "Z auto learned, count=" + data.count);
        await loadBaths();
        await refreshStatus();
    } catch (e) {
        console.error(e);
        setMsg(false, "Z auto learn error: " + e.message);
    }
}

async function relearnZLevel(level) {
    el("zLevelInput").value = level;
    await learnZTag();
}

async function deleteZLevel(level) {
    if (!confirm("Delete Z level " + level + "?")) return;
    setMsg(null, "Deleting Z level " + level + "...");
    try {
        const res = await fetch("/z_tag_delete?level=" + encodeURIComponent(level), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        setMsg(true, "Z level deleted");
        await loadBaths();
        await refreshStatus();
    } catch (e) {
        console.error(e);
        setMsg(false, "Z delete error: " + e.message);
    }
}

async function learnZTag() {
    const level = parseInt(el("zLevelInput").value || "0", 10);
    setMsg(null, "Считывание Z-метки уровня " + level + "...");
    try {
        const res = await fetch("/z_tag_learn?level=" + encodeURIComponent(level), {method: "POST"});
        const txt = await res.text();
        if (!res.ok) throw new Error(txt || ("HTTP " + res.status));
        const data = JSON.parse(txt);
        setMsg(true, "Z-уровень " + data.level + " сохранён");
        await loadBaths();
    } catch (e) {
        console.error(e);
        setMsg(false, "Ошибка обучения Z-уровня: " + e.message);
    }
}

async function refreshStatus() {
    try {
        const res = await fetch("/status");
        if (!res.ok) return;
        const st = await res.json();
        el("currentZLabel").textContent = (st.z_level != null && st.z_level >= 0) ? st.z_level : "—";
    } catch (_) {}
}

document.addEventListener("DOMContentLoaded", () => {
    el("btnAddBath").addEventListener("click", addBath);
    el("btnReload").addEventListener("click", loadBaths);
    el("btnSave").addEventListener("click", saveBaths);
    el("btnLearnStart").addEventListener("click", () => learnServicePoint("start"));
    el("btnLearnEnd").addEventListener("click", () => learnServicePoint("end"));
    el("btnLearnZStart").addEventListener("click", () => learnServicePoint("z_start"));
    el("btnLearnZEnd").addEventListener("click", () => learnServicePoint("z_end"));
    el("btnLearnZ").addEventListener("click", learnZTag);
    el("btnAutoZ").addEventListener("click", autoLearnZ);

    el("bathsTable").addEventListener("click", (e) => {
        const btn = e.target.closest(".btn-learn");
        if (btn) {
            const tr = btn.closest("tr");
            if (!tr) return;
            learnTagForRow(tr);
            return;
        }

        const delBtn = e.target.closest(".btn-delete");
        if (delBtn) {
            const tr = delBtn.closest("tr");
            if (!tr) return;
            deleteBath(tr);
        }
    });

    el("zTagsTable").addEventListener("click", (e) => {
        const relearnBtn = e.target.closest(".btn-z-relearn");
        if (relearnBtn) {
            const tr = relearnBtn.closest("tr");
            if (!tr) return;
            const level = parseInt(tr.dataset.level, 10);
            if (isNaN(level)) return;
            relearnZLevel(level);
            return;
        }

        const delBtn = e.target.closest(".btn-z-delete");
        if (delBtn) {
            const tr = delBtn.closest("tr");
            if (!tr) return;
            const level = parseInt(tr.dataset.level, 10);
            if (isNaN(level)) return;
            deleteZLevel(level);
        }
    });

    loadBaths();
    refreshStatus();
    setInterval(refreshStatus, 1000);
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
    html.replace("{{nav_logs}}", "");

    html.replace("{{content}}", FPSTR(WEB_BATHS_CONTENT));

    return html;
}
