#pragma once
#include <pgmspace.h>

const char WEB_LAYOUT[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <title>{{title}}</title>
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

        /* Меню */
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

        /* Контент */
        .content {
            margin-top: 16px;
        }

        .footer {
            margin-top: 14px;
            font-size: .75rem;
            color: var(--muted);
            text-align: right;
            opacity: .7;
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
        <div class="top-row">
            <div class="brand">
                <h1><span>Galva</span>Control</h1>
                <div class="subtitle">Управление гальванической линией</div>
            </div>
            <div class="pill">
                Wi-Fi: локальная сеть / ESP32
            </div>
        </div>

        <nav>
            <a href="/" class="nav-btn {{nav_index}}">⚙ Панель</a>
            <a href="/monitor" class="nav-btn {{nav_monitor}}">📈 Монитор</a>
            <a href="/routes_ui" class="nav-btn {{nav_routes}}">📋 Рецепты</a>
            <a href="/baths_ui" class="nav-btn {{nav_baths}}">🛰 RFID</a>
            <a href="/baths_autolearn_ui" class="nav-btn {{nav_autolearn}}">📡 Калибровка</a>
        </nav>
    </header>

    <div class="content">
        {{content}}
    </div>

    <div class="footer">
        GalvaControl · ESP32 · промышленная автоматизация
    </div>
</div>
</body>
</html>
)rawliteral";
