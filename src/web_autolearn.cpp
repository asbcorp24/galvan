#include <Arduino.h>
#include "web_layout.h"
#include "web_autolearn.h"

const char WEB_AUTLEARN_CONTENT[] PROGMEM = R"rawliteral(
<section class="card">
    <h2><span class="dot"></span> Автокалибровка линии ванн</h2>

    <p style="color:var(--muted); font-size:0.9rem;">
        Автокалибровка использует отдельные служебные RFID метки <b>Start</b> и <b>End</b>. Всё, что между ними,
        контроллер запоминает как рабочие ванны процесса и нумерует по порядку для рецептов.
    </p>

    <button class="primary" id="btnStart" style="width:100%; margin-bottom:14px;">
        ▶ Начать автокалибровку
    </button>

    <!-- Блок статуса -->
    <div id="stateBox" 
         style="padding:12px; border-radius:10px; background:#0003; margin-bottom:16px;">

        <div style="font-size:1rem; font-weight:600;">
            Состояние: <span id="stState">—</span>
        </div>

        <div style="margin-top:4px; font-size:0.85rem; color:var(--muted);">
            Найдено логических ванн: <span id="stCount">0</span>
        </div>

        <div style="margin-top:10px;">
            <div style="font-size:0.85rem; margin-bottom:4px;">Прогресс:</div>
            <div style="width:100%; height:10px; background:#111; border-radius:6px;">
                <div id="stProgress" 
                     style="height:10px; width:0%; background:var(--accent); border-radius:6px;"></div>
            </div>
        </div>
    </div>

    <!-- Последняя определённая ванна -->
    <h3 style="margin-bottom:6px;">Последняя обнаруженная точка</h3>
    <div id="lastFound" class="bath-card" 
         style="padding:12px; background:rgba(59,167,255,0.12); border:1px solid var(--border); border-radius:10px;">
         Пока нет данных
    </div>

    <!-- Найденные ванны -->
    <h3 style="margin-top:18px;">Текущие найденные ванны</h3>
    <div id="foundList" class="baths-grid"></div>

    <!-- Старая калибровка -->
    <h3 style="margin-top:18px;">Старая калибровка</h3>
    <table style="width:100%; font-size:0.85rem; border-collapse:collapse;">
        <thead>
            <tr style="background:#0004;">
                <th style="padding:6px; text-align:left;">Логич. №</th>
                <th style="padding:6px; text-align:left;">UID</th>
                <th style="padding:6px; text-align:left;">Старт</th>
                <th style="padding:6px; text-align:left;">Конец</th>
            </tr>
        </thead>
        <tbody id="oldTable"></tbody>
    </table>
</section>

<script>
document.getElementById("btnStart").onclick = () => {
    fetch("/baths_autolearn", {method:"POST"})
        .then(r => r.text())
        .then(t => {
            document.getElementById("stState").textContent = 
                "Команда отправлена — ожидаем начало...";
        });
};

function renderLastFound(b){
    if (!b) return;

    const title =
        b.kind_str === "start" ? "Стартовая точка линии" :
        b.kind_str === "end" ? "Конечная точка линии" :
        b.kind_str === "z_level" ? ("Z-уровень " + b.index) :
        ("Рабочая ванна №" + b.index);

    let html = `
        <div><b>${title}</b></div>
        <div style="margin-top:4px;">UID: <span style="color:var(--accent);">${b.uid}</span></div>

        <div style="margin-top:6px; font-size:0.85rem;">
            ${b.isStart ? "<b>Стартовая точка линии</b><br>" : ""}
            ${b.isEnd   ? "<b>Конечная точка линии</b><br>" : ""}
        </div>
    `;
    document.getElementById("lastFound").innerHTML = html;
}

function renderFoundList(list){
    const div = document.getElementById("foundList");
    div.innerHTML = "";
    list.forEach(b => {
        const el = document.createElement("div");
        el.className = "bath-card";
        el.style.padding = "10px";
        el.innerHTML = `
            <div class="bath-header">
                <div class="bath-name">Ванна №${b.index}</div>
                <div class="badge">${b.uid}</div>
            </div>
        `;
        div.appendChild(el);
    });
}

function renderOldTable(list){
    const tbody = document.getElementById("oldTable");
    tbody.innerHTML = "";
    list.forEach(b => {
        tbody.innerHTML += `
            <tr>
                <td style="padding:6px;">${b.index}</td>
                <td style="padding:6px;">${b.uid}</td>
                <td style="padding:6px;">${b.isStart ? "✔" : "—"}</td>
                <td style="padding:6px;">${b.isEnd   ? "✔" : "—"}</td>
            </tr>
        `;
    });
}

setInterval(() => {
    fetch("/status")
        .then(r => r.json())
        .then(st => {
            document.getElementById("stState").textContent = st.state_str;
            document.getElementById("stCount").textContent = st.learn_count;

            let prog = Math.min((st.learn_count / 10) * 100, 100);
            document.getElementById("stProgress").style.width = prog + "%";

            if (st.last_found) renderLastFound(st.last_found);
            if (st.baths) {
                renderFoundList(st.baths);
                renderOldTable(st.baths);
            }
        })
}, 500);
</script>
)rawliteral";



String renderPageAutoLearn() {
    String html = FPSTR(WEB_LAYOUT);

    html.replace("{{title}}", "Калибровка ванн");
    html.replace("{{nav_autolearn}}", "active");

    html.replace("{{nav_index}}", "");
    html.replace("{{nav_monitor}}", "");
    html.replace("{{nav_routes}}", "");
    html.replace("{{nav_baths}}", "");
    html.replace("{{nav_logs}}", "");

    html.replace("{{content}}", FPSTR(WEB_AUTLEARN_CONTENT));

    return html;
}
