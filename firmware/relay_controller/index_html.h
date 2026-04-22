// index_html.h
// Single-page web UI, served from PROGMEM. Pure vanilla HTML/CSS/JS —
// no external CDN, works entirely inside the ESP32's AP with no internet.

#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AccBus Console</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: ui-monospace, "SF Mono", Menlo, monospace;
         background: #0b0f14; color: #cfe2b0; font-size: 13px; }
  header { display: flex; flex-wrap: wrap; gap: 10px 18px; padding: 10px 14px;
           background: #0f1a24; border-bottom: 1px solid #1e2a38; align-items: center; }
  header b { color: #9bd770; }
  .pill { padding: 2px 8px; border-radius: 10px; border: 1px solid #2a3a4a;
          font-size: 11px; background: #0b131c; }
  .ok   { color: #9bd770; border-color: #355e1f; }
  .bad  { color: #f29900; border-color: #644100; }
  .warn { color: #ffd166; border-color: #705200; }
  main { display: flex; flex-direction: column; height: calc(100vh - 52px); }
  #log { flex: 1; overflow-y: auto; padding: 6px 14px; font-size: 12.5px; line-height: 1.5; }
  .row { white-space: pre; }
  .row.tx  { color: #9bd770; }
  .row.rx  { color: #cfe2b0; }
  .row.rxw { color: #1ab4ff; }
  .row.err { color: #ff6b6b; }
  .row.inf { color: #808a95; }
  #bar { display: flex; gap: 6px; padding: 8px 10px; border-top: 1px solid #1e2a38;
         background: #0f1a24; flex-wrap: wrap; }
  #bar button { background: #1a2a3a; color: #cfe2b0; border: 1px solid #2a3a4a;
                padding: 4px 10px; border-radius: 4px; font: inherit; cursor: pointer; }
  #bar button:hover { background: #253a52; }
  #input-row { display: flex; padding: 8px 14px; border-top: 1px solid #1e2a38;
               background: #0b131c; align-items: center; gap: 8px; }
  #prompt { color: #9bd770; font-weight: bold; }
  #cmd { flex: 1; background: transparent; color: #cfe2b0; border: none; outline: none;
         font: inherit; padding: 4px 0; }
  #help { color: #808a95; font-size: 11px; padding: 0 14px 6px; }
  label.toggle { display: inline-flex; align-items: center; gap: 6px; color: #cfe2b0; }
  .tabs { display: flex; gap: 0; border-bottom: 1px solid #1e2a38; background: #0f1a24; }
  .tab { padding: 6px 18px; cursor: pointer; color: #808a95; border-bottom: 2px solid transparent;
         font: inherit; background: none; border-top: none; border-left: none; border-right: none; }
  .tab.active { color: #9bd770; border-bottom-color: #9bd770; }
  .tab:hover { color: #cfe2b0; }
  #serial-log { flex: 1; overflow-y: auto; padding: 6px 14px; font-size: 12.5px;
                line-height: 1.5; white-space: pre-wrap; word-break: break-all;
                color: #a0b0c0; display: none; }
</style>
</head>
<body>
<header>
  <span>node: <b id="node-name">…</b></span>
  <span>id: <b id="node-id">…</b></span>
  <span class="pill" id="p-can">can ?</span>
  <span class="pill" id="p-wifi">wifi peer ?</span>
  <label class="toggle"><input type="checkbox" id="wifi-only"> force wifi-only</label>
  <span style="margin-left:auto;color:#808a95" id="fps">—</span>
</header>
<main>
  <div class="tabs">
    <button class="tab active" id="tab-can" onclick="switchTab('can')">CAN Frames</button>
    <button class="tab" id="tab-serial" onclick="switchTab('serial')">Serial</button>
  </div>
  <div id="help">
    Enter a CAN frame as hex bytes: <b>&lt;id&gt; &lt;byte0&gt; &lt;byte1&gt;…</b>  (up to 8 bytes).<br>
    Or use an alias: <b>:relay &lt;n&gt; on|off</b> · <b>:alloff</b> · <b>:horn</b>  ·
    <b>:readcfg sw|relay</b> · <b>:save sw|relay</b> · <b>:reset sw|relay</b> ·
    <b>:viper lock|unlock|start</b>
  </div>
  <div id="log"></div>
  <div id="serial-log"></div>
  <div id="bar">
    <button onclick="runAlias(':relay 1 on')">R1 ON</button>
    <button onclick="runAlias(':relay 1 off')">R1 OFF</button>
    <button onclick="runAlias(':relay 5 on')">HORN ON</button>
    <button onclick="runAlias(':relay 5 off')">HORN OFF</button>
    <button onclick="runAlias(':alloff')">ALL OFF</button>
    <button onclick="runAlias(':readcfg sw')">READ SW CFG</button>
    <button onclick="runAlias(':readcfg relay')">READ RELAY CFG</button>
    <button onclick="runAlias(':viper lock')">VIPER LOCK</button>
    <button onclick="runAlias(':viper unlock')">VIPER UNLOCK</button>
    <button onclick="runAlias(':viper start')">VIPER START</button>
    <button onclick="clearLog()">CLEAR</button>
  </div>
  <div id="input-row">
    <span id="prompt">&gt;</span>
    <input id="cmd" autocomplete="off" spellcheck="false" placeholder="100 01 01">
  </div>
</main>

<script>
const log   = document.getElementById('log');
const slog  = document.getElementById('serial-log');
const cmd   = document.getElementById('cmd');
const wifiOnly = document.getElementById('wifi-only');
let sinceSeq = 0;
let serialCursor = 0;
let activeTab = 'can';
let history  = [];
let histIdx  = -1;

// ---------- tabs ----------
function switchTab(tab) {
  activeTab = tab;
  document.getElementById('tab-can').className    = 'tab' + (tab==='can'    ? ' active' : '');
  document.getElementById('tab-serial').className = 'tab' + (tab==='serial' ? ' active' : '');
  document.getElementById('log').style.display        = tab==='can'    ? '' : 'none';
  document.getElementById('help').style.display       = tab==='can'    ? '' : 'none';
  document.getElementById('serial-log').style.display = tab==='serial' ? '' : 'none';
  document.getElementById('bar').style.display        = tab==='can'    ? '' : 'none';
  document.getElementById('input-row').style.display  = tab==='can'    ? '' : 'none';
}

// ---------- rendering ----------
function append(cls, text) {
  const d = document.createElement('div');
  d.className = 'row ' + cls;
  d.textContent = text;
  log.appendChild(d);
  // cap to last 400 lines
  while (log.childElementCount > 400) log.removeChild(log.firstChild);
  log.scrollTop = log.scrollHeight;
}
function clearLog() { log.innerHTML = ''; }

function fmtFrame(f) {
  const hex = f.data.map(b => b.toString(16).padStart(2,'0')).join(' ');
  const id  = '0x' + f.id.toString(16).padStart(3, '0').toUpperCase();
  const ts  = new Date(f.t).toISOString().slice(11, 23);
  const dir = f.out ? 'TX' : 'RX';
  const src = f.src.padEnd(4);
  return `${ts} ${dir} ${src} ${id}  [${f.dlc}] ${hex}`;
}

// ---------- API ----------
async function pollFrames() {
  try {
    const r = await fetch('/api/frames?since=' + sinceSeq);
    const j = await r.json();
    sinceSeq = j.cursor;
    for (const f of j.frames) {
      const cls = f.out ? 'tx' : (f.src === 'wifi' ? 'rxw' : 'rx');
      append(cls, fmtFrame(f));
    }
  } catch (e) { /* ignore */ }
}
async function pollStatus() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    document.getElementById('node-name').textContent = s.name;
    document.getElementById('node-id').textContent   = '0x' + s.id.toString(16).padStart(2, '0');
    const pc = document.getElementById('p-can');
    pc.textContent = 'can ' + (s.can_ok ? 'OK' : 'DOWN');
    pc.className = 'pill ' + (s.can_ok ? 'ok' : 'bad');
    const pw = document.getElementById('p-wifi');
    pw.textContent = 'wifi peer ' + (s.wifi_peer ? 'OK' : 'IDLE');
    pw.className = 'pill ' + (s.wifi_peer ? 'ok' : 'warn');
    wifiOnly.checked = !!s.wifi_only;
    document.getElementById('fps').textContent = s.uptime_s + 's up';
  } catch (e) { /* ignore */ }
}
async function sendFrame(id, data) {
  const r = await fetch('/api/send', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ id, data })
  });
  if (!r.ok) append('err', 'send failed: ' + r.status);
}
async function pollSerial() {
  try {
    const r = await fetch('/api/serial?since=' + serialCursor);
    const j = await r.json();
    serialCursor = j.cursor;
    if (j.text) {
      slog.textContent += j.text;
      // cap to ~8000 chars
      if (slog.textContent.length > 8000)
        slog.textContent = slog.textContent.slice(-6000);
      slog.scrollTop = slog.scrollHeight;
    }
  } catch (e) { /* ignore */ }
}
async function setWifiOnly(on) {
  await fetch('/api/wifi_only', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ on })
  });
}

// ---------- command parsing ----------
function parseHex(tok) {
  const n = parseInt(tok, 16);
  if (isNaN(n)) throw new Error('not hex: ' + tok);
  return n;
}
function parseRawFrame(line) {
  const toks = line.trim().split(/\s+/);
  if (toks.length < 1) throw new Error('empty');
  const id = parseHex(toks[0]);
  const data = toks.slice(1).map(parseHex);
  if (data.length > 8) throw new Error('max 8 data bytes');
  return { id, data };
}
function aliasToFrame(line) {
  // :relay <n> on|off
  let m;
  if ((m = /^:relay\s+(\d+)\s+(on|off)\s*$/i.exec(line))) {
    const n = parseInt(m[1]);
    if (n < 1 || n > 6) throw new Error('relay 1..6');
    const mask  = 1 << (n - 1);
    const state = m[2].toLowerCase() === 'on' ? mask : 0;
    return { id: 0x100, data: [mask, state] };
  }
  if (/^:alloff\s*$/i.test(line)) return { id: 0x100, data: [0x3F, 0x00] };
  if (/^:horn\s*$/i.test(line))   return { id: 0x100, data: [0x10, 0x10] };
  if ((m = /^:readcfg\s+(sw|relay)\s*$/i.exec(line))) {
    const target = m[1].toLowerCase() === 'sw' ? 0x01 : 0x02;
    const key    = m[1].toLowerCase() === 'sw' ? 0x10 : 0x20;
    return { id: 0x401, data: [target, key, 0xFF] };
  }
  if ((m = /^:save\s+(sw|relay)\s*$/i.exec(line))) {
    const target = m[1].toLowerCase() === 'sw' ? 0x01 : 0x02;
    return { id: 0x403, data: [target, 0x01] };
  }
  if ((m = /^:reset\s+(sw|relay)\s*$/i.exec(line))) {
    const target = m[1].toLowerCase() === 'sw' ? 0x01 : 0x02;
    return { id: 0x403, data: [target, 0x03] };
  }
  // :cfgsw <idx> toggle|pulse|hold|scene|event <arg> [arg2]
  if ((m = /^:cfgsw\s+(\d+)\s+(toggle|pulse|event|hold|scene)\s+(\d+)(?:\s+(\d+))?\s*(\!)?\s*$/i.exec(line))) {
    const idx = +m[1];
    const kindMap = { toggle:0, pulse:1, event:2, hold:3, scene:4 };
    const kind = kindMap[m[2].toLowerCase()];
    const arg  = +m[3];
    const arg2 = m[4] ? +m[4] : 0;
    const flags = m[5] === '!' ? 0x01 : 0x00;
    return { id: 0x400, data: [0x01, 0x10, idx, kind, arg, arg2 & 0xFF, (arg2>>8)&0xFF, flags] };
  }
  // :cfgrelay <idx> maxon <ms> [!]
  if ((m = /^:cfgrelay\s+(\d+)\s+maxon\s+(\d+)\s*(\!)?\s*$/i.exec(line))) {
    const idx = +m[1];
    const ms  = +m[2];
    const flags = m[3] === '!' ? 0x01 : 0x00;
    return { id: 0x400, data: [0x02, 0x20, idx, 0, 0, ms & 0xFF, (ms>>8)&0xFF, flags] };
  }
  // :viper lock|unlock|start
  if ((m = /^:viper\s+(lock|unlock|start)\s*$/i.exec(line))) {
    const cmdMap = { lock: 0x01, unlock: 0x02, start: 0x03 };
    return { id: 0x510, data: [cmdMap[m[1].toLowerCase()]] };
  }
  throw new Error('unknown alias: ' + line);
}
async function runAlias(line) {
  try {
    const f = aliasToFrame(line);
    append('inf', '  ' + line);
    await sendFrame(f.id, f.data);
  } catch (e) {
    append('err', '  ' + e.message);
  }
}
async function submit() {
  const line = cmd.value.trim();
  if (!line) return;
  cmd.value = '';
  history.push(line); histIdx = history.length;
  try {
    const f = line.startsWith(':') ? aliasToFrame(line) : parseRawFrame(line);
    append('inf', '> ' + line);
    await sendFrame(f.id, f.data);
  } catch (e) {
    append('err', '! ' + e.message);
  }
}

// ---------- events ----------
cmd.addEventListener('keydown', e => {
  if (e.key === 'Enter') { submit(); }
  else if (e.key === 'ArrowUp')   { if (histIdx > 0) { histIdx--; cmd.value = history[histIdx]; } }
  else if (e.key === 'ArrowDown') { if (histIdx < history.length - 1) { histIdx++; cmd.value = history[histIdx]; } else { histIdx = history.length; cmd.value = ''; } }
});
wifiOnly.addEventListener('change', () => setWifiOnly(wifiOnly.checked));

pollStatus(); setInterval(pollStatus, 1500);
pollFrames(); setInterval(pollFrames, 250);
pollSerial(); setInterval(pollSerial, 500);
cmd.focus();
</script>
</body>
</html>
)HTMLPAGE";
