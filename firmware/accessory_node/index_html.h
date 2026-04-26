// index_html.h
// Single-page web UI, served from PROGMEM. Pure vanilla HTML/CSS/JS —
// no external CDN, works entirely inside the ESP32's AP with no internet.

#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
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
  .txm-group { display: inline-flex; border-radius: 6px; overflow: hidden; border: 1px solid #2a3a4a; }
  .txm-btn { background: #0b131c; color: #808a95; border: none; border-right: 1px solid #2a3a4a;
             padding: 2px 8px; font: inherit; font-size: 11px; cursor: pointer; }
  .txm-btn:last-child { border-right: none; }
  .txm-btn.active { background: #1e3a1e; color: #9bd770; }
  .tabs { display: flex; gap: 0; border-bottom: 1px solid #1e2a38; background: #0f1a24; }
  .tab { padding: 6px 18px; cursor: pointer; color: #808a95; border-bottom: 2px solid transparent;
         font: inherit; background: none; border-top: none; border-left: none; border-right: none; }
  .tab.active { color: #9bd770; border-bottom-color: #9bd770; }
  .tab:hover { color: #cfe2b0; }
  #serial-log { flex: 1; overflow-y: auto; padding: 6px 14px; font-size: 12.5px;
                line-height: 1.5; white-space: pre-wrap; word-break: break-all;
                color: #a0b0c0; display: none; }
  /* --- Control tab --- */
  #control-panel { flex: 1; overflow-y: auto; display: none; }
  .ctrl-section { padding: 14px 14px; border-bottom: 1px solid #1e2a38; }
  .ctrl-head { font-size: 10px; font-weight: bold; color: #9bd770; letter-spacing: 1px;
               text-transform: uppercase; margin-bottom: 12px; }
  .relay-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(110px, 1fr)); gap: 8px;
                margin-bottom: 10px; }
  .relay-tile { background: #0f1a24; border: 1px solid #2a3a4a; border-radius: 6px;
                padding: 10px 8px; display: flex; flex-direction: column;
                align-items: center; gap: 5px; }
  .relay-tile.on { border-color: #355e1f; background: #0a180a; }
  .rt-led { width: 12px; height: 12px; border-radius: 50%;
            background: #1a2a3a; border: 2px solid #2a3a4a; flex-shrink: 0; }
  .relay-tile.on .rt-led { background: #9bd770; border-color: #6ba840;
                            box-shadow: 0 0 8px #4a8020; }
  .rt-name { font-size: 10px; color: #808a95; text-align: center; width: 100%;
             overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .rt-state { font-size: 12px; font-weight: bold; color: #2a3a4a; }
  .relay-tile.on .rt-state { color: #9bd770; }
  .rt-btn { background: #1a2a3a; color: #cfe2b0; border: 1px solid #2a3a4a;
            padding: 3px 10px; border-radius: 4px; font: inherit; font-size: 11px; cursor: pointer; }
  .rt-btn:hover { background: #253a52; }
  .ctrl-btns { display: flex; gap: 6px; flex-wrap: wrap; }
  .ctrl-btn { background: #1a2a3a; color: #cfe2b0; border: 1px solid #2a3a4a;
              padding: 5px 14px; border-radius: 4px; font: inherit; font-size: 12px; cursor: pointer; }
  .ctrl-btn:hover { background: #253a52; }
  .ctrl-btn.danger { border-color: #644100; color: #f29900; }
  .ctrl-btn.danger:hover { background: #2a1800; }
  /* switch inputs */
  .sw-input-grid { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 4px; }
  .sw-input { background: #0f1a24; border: 2px solid #2a3a4a; border-radius: 6px;
              padding: 8px 10px; display: flex; flex-direction: column;
              align-items: center; gap: 3px; min-width: 58px; cursor: pointer; }
  .sw-input:hover { background: #152030; }
  .sw-input.on { border-color: #355e1f; background: #0a180a; }
  .sw-input.on:hover { background: #0f2010; }
  .swi-label { font-size: 10px; color: #808a95; }
  .swi-state { font-size: 12px; font-weight: bold; color: #2a3a4a; }
  .sw-input.on .swi-state { color: #9bd770; }
  /* buttons */
  .btn-input-grid { display: flex; flex-wrap: wrap; gap: 8px; }
  .btn-input { background: #0f1a24; border: 2px solid #2a3a4a; border-radius: 20px;
               padding: 6px 16px; font: inherit; font-size: 12px; color: #808a95; cursor: pointer; }
  .btn-input:hover { background: #152030; }
  .btn-input.pressed { background: #0a1a2a; border-color: #1ab4ff; color: #1ab4ff; }
  /* status LEDs */
  .led-grid { display: flex; gap: 16px; flex-wrap: wrap; }
  .led-indicator { display: flex; flex-direction: column; align-items: center; gap: 5px; }
  .led-dot { width: 18px; height: 18px; border-radius: 50%;
             background: #1a2a3a; border: 2px solid #2a3a4a; cursor: pointer; }
  .led-dot:hover { border-color: #4a6a8a; }
  .led-dot.on { background: #9bd770; border-color: #6ba840; box-shadow: 0 0 8px #4a8020; }
  .led-dot.on:hover { border-color: #9bd770; }
  .led-label { font-size: 10px; color: #808a95; }
  .viper-btns { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 10px; }
  .viper-btn { background: #1a2a3a; color: #cfe2b0; border: 1px solid #2a3a4a;
               padding: 7px 16px; border-radius: 4px; font: inherit; cursor: pointer; }
  .viper-btn:hover { background: #253a52; }
  .viper-btn.lock   { border-color: #355e1f; color: #9bd770; }
  .viper-btn.unlock { border-color: #644100; color: #f29900; }
  .viper-btn.start  { border-color: #1a4a6a; color: #1ab4ff; }
  .viper-last { font-size: 11px; color: #808a95; }
  /* --- Rules tab --- */
  #rules-panel { flex: 1; overflow-y: auto; display: none; padding: 14px; }
  .rules-toolbar { display: flex; gap: 8px; margin-bottom: 14px; flex-wrap: wrap; }
  .rule-row { background: #0f1a24; border: 1px solid #1e2a38; border-radius: 6px;
              padding: 10px 12px; margin-bottom: 8px; display: flex;
              align-items: flex-start; gap: 10px; }
  .rule-row.empty { opacity: 0.35; }
  .rule-idx { font-size: 10px; color: #4a6a8a; min-width: 22px; padding-top: 2px; }
  .rule-desc { flex: 1; }
  .rule-trig { font-size: 12px; color: #1ab4ff; }
  .rule-act  { font-size: 12px; color: #9bd770; margin-top: 2px; }
  .rule-empty-text { font-size: 11px; color: #4a6a8a; font-style: italic; }
  .rule-btns { display: flex; gap: 5px; }
  .rule-edit-btn { background:#1a2a3a; color:#cfe2b0; border:1px solid #2a3a4a;
                   padding:2px 8px; border-radius:4px; font:inherit; font-size:11px; cursor:pointer; }
  .rule-edit-btn:hover { background:#253a52; }
  .rule-del-btn  { background:#1a0a0a; color:#f29900; border:1px solid #644100;
                   padding:2px 8px; border-radius:4px; font:inherit; font-size:11px; cursor:pointer; }
  .rule-del-btn:hover { background:#2a1000; }
  /* editor */
  .rule-editor { background:#0a1520; border:1px solid #2a4a6a; border-radius:6px;
                 padding:12px; margin-bottom:12px; }
  .rule-editor h4 { margin:0 0 10px; color:#9bd770; font-size:12px; }
  .re-row { display:flex; gap:8px; margin-bottom:8px; flex-wrap:wrap; align-items:center; }
  .re-row label { font-size:11px; color:#808a95; min-width:70px; }
  .re-row select, .re-row input[type=number] {
    background:#0f1a24; color:#cfe2b0; border:1px solid #2a3a4a; border-radius:4px;
    padding:3px 6px; font:inherit; font-size:12px; }
  .re-row select { min-width:160px; }
  .re-row input[type=number] { width:70px; }
  .re-save { background:#1a3a1a; color:#9bd770; border:1px solid #355e1f;
             padding:4px 14px; border-radius:4px; font:inherit; cursor:pointer; }
  .re-save:hover { background:#254a25; }
  .re-cancel { background:#1a2a3a; color:#808a95; border:1px solid #2a3a4a;
               padding:4px 12px; border-radius:4px; font:inherit; cursor:pointer; }
  .re-cancel:hover { background:#253a52; }
</style>
</head>
<body>
<header>
  <span>node: <b id="node-name">…</b></span>
  <span>id: <b id="node-id">…</b></span>
  <span class="pill" id="p-can">can ?</span>
  <span class="pill" id="p-wifi">wifi peer ?</span>
  <div class="txm-group" title="CAN transmission mode">
    <button class="txm-btn" id="txm-0" onclick="setTxMode(0)">CAN+WiFi</button>
    <button class="txm-btn" id="txm-1" onclick="setTxMode(1)">WiFi Only</button>
    <button class="txm-btn" id="txm-2" onclick="setTxMode(2)">CAN Only</button>
  </div>
  <span style="margin-left:auto;color:#808a95" id="fps">—</span>
</header>
<main>
  <div class="tabs">
    <button class="tab active" id="tab-can" onclick="switchTab('can')">CAN Frames</button>
    <button class="tab" id="tab-serial" onclick="switchTab('serial')">Serial</button>
    <button class="tab" id="tab-control" onclick="switchTab('control')">Control</button>
    <button class="tab" id="tab-rules" onclick="switchTab('rules')">Rules</button>
  </div>
  <div id="help">
    Enter a CAN frame as hex bytes: <b>&lt;id&gt; &lt;byte0&gt; &lt;byte1&gt;…</b>  (up to 8 bytes).<br>
    Or use an alias: <b>:relay &lt;n&gt; on|off</b> · <b>:alloff</b> · <b>:horn</b>  ·
    <b>:readcfg sw|relay</b> · <b>:save sw|relay</b> · <b>:reset sw|relay</b> ·
    <b>:viper lock|unlock|start</b>
  </div>
  <div id="log"></div>
  <div id="serial-log"></div>
  <div id="control-panel"></div>
  <div id="rules-panel"></div>
  <div id="bar">
    <button onclick="runAlias(':relay 1 on')">R1 ON</button>
    <button onclick="runAlias(':relay 1 off')">R1 OFF</button>
    <button onclick="runAlias(':relay 5 on')">HORN ON</button>
    <button onclick="runAlias(':relay 5 off')">HORN OFF</button>
    <button onclick="runAlias(':alloff')">ALL OFF</button>
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
let currentTxMode = 0;
let sinceSeq = 0;
let serialCursor = 0;
let activeTab = 'can';
let history  = [];
let histIdx  = -1;

// ---------- tabs ----------
function switchTab(tab) {
  activeTab = tab;
  ['can','serial','control','rules'].forEach(t => {
    document.getElementById('tab-' + t).className = 'tab' + (tab===t ? ' active' : '');
  });
  document.getElementById('log').style.display            = tab==='can'     ? '' : 'none';
  document.getElementById('help').style.display           = tab==='can'     ? '' : 'none';
  document.getElementById('serial-log').style.display     = tab==='serial'  ? 'block' : 'none';
  document.getElementById('control-panel').style.display  = tab==='control' ? 'block' : 'none';
  document.getElementById('rules-panel').style.display    = tab==='rules'   ? 'block' : 'none';
  document.getElementById('bar').style.display            = tab==='can'     ? '' : 'none';
  document.getElementById('input-row').style.display      = tab==='can'     ? '' : 'none';
  if (tab === 'rules') loadRules();
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

// ---------- control tab state ----------
let cfg    = null;
let nodeId = 0;
let relayMask   = 0;
let switchState = new Array(16).fill(false);  // indexed by switch_id from SWITCH_EVENT
let ledState    = 0;                          // bitmap from LED_STATUS for this node

function updateRelayTiles() {
  if (!cfg || !cfg.has_relay) return;
  for (let i = 0; i < 6; i++) {
    const on = (relayMask >> i) & 1;
    const tile = document.getElementById('rt-' + i);
    if (tile) tile.className = 'relay-tile' + (on ? ' on' : '');
    const st = document.getElementById('rs-' + i);
    if (st) st.textContent = on ? 'ON' : 'OFF';
  }
}

function updateSwitchDisplay() {
  if (!cfg || !cfg.has_switches) return;
  for (let i = 0; i < cfg.switch_count; i++) {
    const el = document.getElementById('swi-' + i);
    const st = document.getElementById('sws-' + i);
    const on = switchState[i];
    if (el) el.className = 'sw-input' + (on ? ' on' : '');
    if (st) st.textContent = on ? 'ON' : 'OFF';
  }
  for (let i = 0; i < cfg.button_count; i++) {
    const idx = cfg.switch_count + i;
    const el = document.getElementById('bti-' + idx);
    if (el) el.className = 'btn-input' + (switchState[idx] ? ' pressed' : '');
  }
}

function updateLedDisplay() {
  if (!cfg || !cfg.has_leds) return;
  for (let i = 0; i < cfg.led_count; i++) {
    const el = document.getElementById('led-' + i);
    if (el) el.className = 'led-dot' + ((ledState >> i) & 1 ? ' on' : '');
  }
}

function buildControlPanel() {
  if (!cfg) return;
  let h = '';

  // Relays — only on nodes with relay hardware
  if (cfg.has_relay) {
    h += '<div class="ctrl-section">';
    h += '<div class="ctrl-head">Relays</div>';
    h += '<div class="relay-grid">';
    for (let i = 0; i < 6; i++) {
      h += `<div class="relay-tile" id="rt-${i}">`;
      h +=   `<div class="rt-led"></div>`;
      h +=   `<div class="rt-name">${cfg.relay_labels[i]}</div>`;
      h +=   `<div class="rt-state" id="rs-${i}">OFF</div>`;
      h +=   `<button class="rt-btn" onclick="toggleRelay(${i})">Toggle</button>`;
      h += '</div>';
    }
    h += '</div>';
    h += '<div class="ctrl-btns">';
    h += '<button class="ctrl-btn danger" onclick="sendAllOff()">All OFF</button>';
    h += '</div>';
    h += '</div>';
  }

  // Latching switch inputs — read-only physical state
  if (cfg.has_switches && cfg.switch_count > 0) {
    h += '<div class="ctrl-section">';
    h += '<div class="ctrl-head">Switch Inputs</div>';
    h += '<div class="sw-input-grid">';
    for (let i = 0; i < cfg.switch_count; i++) {
      h += `<div class="sw-input" id="swi-${i}" onclick="simSwitch(${i})">`;
      h +=   `<div class="swi-label">SW${i + 1}</div>`;
      h +=   `<div class="swi-state" id="sws-${i}">OFF</div>`;
      h += '</div>';
    }
    h += '</div>';
    h += '</div>';
  }

  // Momentary buttons — read-only, highlight while held
  if (cfg.has_switches && cfg.button_count > 0) {
    h += '<div class="ctrl-section">';
    h += '<div class="ctrl-head">Buttons</div>';
    h += '<div class="btn-input-grid">';
    for (let i = 0; i < cfg.button_count; i++) {
      const idx = cfg.switch_count + i;
      h += `<div class="btn-input" id="bti-${idx}" onclick="simButton(${idx})">BTN${i + 1}</div>`;
    }
    h += '</div>';
    h += '</div>';
  }

  // Status LEDs
  if (cfg.has_leds && cfg.led_count > 0) {
    h += '<div class="ctrl-section">';
    h += '<div class="ctrl-head">Status LEDs</div>';
    h += '<div class="led-grid">';
    for (let i = 0; i < cfg.led_count; i++) {
      h += `<div class="led-indicator">`;
      h +=   `<div class="led-dot" id="led-${i}" onclick="toggleLed(${i})"></div>`;
      h +=   `<div class="led-label">LED${i + 1}</div>`;
      h += '</div>';
    }
    h += '</div>';
    h += '</div>';
  }

  // Viper alarm — viper interface node only
  if (cfg.has_viper) {
    h += '<div class="ctrl-section">';
    h += '<div class="ctrl-head">Alarm</div>';
    h += '<div class="viper-btns">';
    h += '<button class="viper-btn lock"   onclick="viperCmd(1)">Lock / Arm</button>';
    h += '<button class="viper-btn unlock" onclick="viperCmd(2)">Unlock / Disarm</button>';
    h += '<button class="viper-btn start"  onclick="viperCmd(3)">Remote Start</button>';
    h += '</div>';
    h += '<div class="viper-last" id="viper-last">No response yet</div>';
    h += '</div>';
  }

  document.getElementById('control-panel').innerHTML = h;
  updateRelayTiles();
  updateSwitchDisplay();
  updateLedDisplay();
}

async function initControl() {
  try {
    const r = await fetch('/api/config');
    cfg = await r.json();
    buildControlPanel();
    if (!cfg.has_rules) document.getElementById('tab-rules').style.display = 'none';
  } catch(e) {}
}

// ---------- control actions ----------
function toggleRelay(idx) {
  const on = (relayMask >> idx) & 1;
  const mask = 1 << idx;
  sendFrame(0x100, [mask, on ? 0 : mask]);
}

function sendAllOff() {
  sendFrame(0x100, [0x3F, 0x00]);
}

// Simulate a latching switch toggle: sends PRESS or RELEASE depending on current state.
function simSwitch(idx) {
  const on = switchState[idx];
  switchState[idx] = !on;
  sendFrame(0x200, [idx, on ? 0 : 1]);  // 0=RELEASE, 1=PRESS
  updateSwitchDisplay();
}

// Simulate a momentary button: brief PRESS then RELEASE after 150 ms.
function simButton(idx) {
  switchState[idx] = true;
  updateSwitchDisplay();
  sendFrame(0x200, [idx, 1]);
  setTimeout(() => {
    switchState[idx] = false;
    updateSwitchDisplay();
    sendFrame(0x200, [idx, 0]);
  }, 150);
}

// Toggle an LED by sending LED_CMD to this node.
function toggleLed(idx) {
  const on = (ledState >> idx) & 1;
  const mask = 1 << idx;
  sendFrame(0x102, [nodeId, mask, on ? 0 : mask]);
}

function viperCmd(cmd) {
  sendFrame(0x510, [cmd]);
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
      // Relay state (for relay controller nodes)
      if (f.id === 0x101 && f.dlc >= 1) {
        relayMask = f.data[0];
        updateRelayTiles();
      } else if (f.id === 0x100 && f.dlc >= 2) {
        relayMask = (relayMask & ~f.data[0]) | (f.data[1] & f.data[0]);
        updateRelayTiles();
      }
      // Switch physical state (press/release from switch panel)
      if (f.id === 0x200 && f.dlc >= 2) {
        const sw = f.data[0], ev = f.data[1];
        switchState[sw] = (ev === 1 || ev === 2);  // 1=PRESS, 2=LONG_PRESS → active; 0=RELEASE → not
        updateSwitchDisplay();
      }
      // LED state for this node
      if (f.id === 0x103 && f.dlc >= 2 && f.data[0] === nodeId) {
        ledState = f.data[1];
        updateLedDisplay();
      }
      // Viper status response
      if (f.id === 0x511) {
        const hex = f.data.slice(0, f.dlc).map(b => b.toString(16).padStart(2,'0')).join(' ');
        const el = document.getElementById('viper-last');
        if (el) el.textContent = 'Last response: ' + hex;
      }
    }
  } catch (e) { /* ignore */ }
}
async function pollStatus() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    nodeId = s.id;
    document.getElementById('node-name').textContent = s.name;
    document.getElementById('node-id').textContent   = '0x' + s.id.toString(16).padStart(2, '0');
    const pc = document.getElementById('p-can');
    pc.textContent = 'can ' + (s.can_ok ? 'OK' : 'DOWN');
    pc.className = 'pill ' + (s.can_ok ? 'ok' : 'bad');
    const pw = document.getElementById('p-wifi');
    pw.textContent = 'wifi peer ' + (s.wifi_peer ? 'OK' : 'IDLE');
    pw.className = 'pill ' + (s.wifi_peer ? 'ok' : 'warn');
    const m = s.tx_mode || 0;
    if (m !== currentTxMode) { currentTxMode = m; updateTxModeButtons(m); }
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
function updateTxModeButtons(mode) {
  for (let i = 0; i < 3; i++) {
    const b = document.getElementById('txm-' + i);
    if (b) b.className = 'txm-btn' + (i === mode ? ' active' : '');
  }
}
async function setTxMode(mode) {
  updateTxModeButtons(mode);
  currentTxMode = mode;
  await fetch('/api/tx_mode', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({ mode })
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

// ---------- rules tab ----------
const CAN_NAMES = {
  0x100:'RELAY_CMD', 0x101:'RELAY_STATUS', 0x102:'LED_CMD', 0x103:'LED_STATUS',
  0x200:'SWITCH_EVENT', 0x201:'ENCODER_EVENT',
  0x300:'TELEMETRY', 0x301:'ENV_DATA', 0x302:'IMU_DATA', 0x303:'SHAKE_EVENT',
  0x510:'VIPER_CMD', 0x511:'VIPER_STATUS'
};
const SW_EVENTS = ['Release','Press','Long Press','Double Press'];
const ACT_NAMES = ['—','Relay Toggle','Relay On','Relay Off','All Off','Relay Scene',
                   'LED On','LED Off','WiFi Enable','WiFi Disable','Viper Cmd',
                   'Menu Select','Menu Enter'];
const NODE_NAMES = {1:'switch-panel', 2:'relay-ctrl', 3:'viper-iface', 255:'broadcast'};

function describeTrigger(r) {
  if (!r.trig_id) return null;
  const id = r.trig_id;
  // Common patterns
  if (id === 0x200 && r.c0_mask === 0xFF && r.c1_mask === 0xFF) {
    const sw = 'SW' + (r.c0_val < 6 ? (r.c0_val+1) : ('BTN'+(r.c0_val-5)));
    return sw + ' ' + (SW_EVENTS[r.c1_val] || 'ev'+r.c1_val);
  }
  if (id === 0x101 && r.c0_mask !== 0) {
    const bit = Math.log2(r.c0_mask);
    if (Number.isInteger(bit))
      return 'Relay '+(bit+1)+' '+(r.c0_val ? 'On' : 'Off')+' (status)';
  }
  if (id === 0x100 && r.c0_mask !== 0 && r.c1_mask !== 0) {
    const bit = Math.log2(r.c0_mask);
    if (Number.isInteger(bit))
      return 'Relay '+(bit+1)+' Cmd '+(r.c1_val ? 'On' : 'Off');
  }
  const name = CAN_NAMES[id] || ('0x'+id.toString(16).toUpperCase());
  let s = name;
  if (r.c0_mask) s += ', b'+r.c0_byte+'='+r.c0_val+'&'+r.c0_mask;
  if (r.c1_mask) s += ', b'+r.c1_byte+'='+r.c1_val+'&'+r.c1_mask;
  return s;
}

function describeAction(r) {
  const name = ACT_NAMES[r.action] || ('act'+r.action);
  if ([1,2,3].includes(r.action)) return name + ' R' + (r.arg0+1);
  if (r.action === 5) return 'Scene 0b'+r.arg0.toString(2).padStart(6,'0');
  if ([6,7].includes(r.action)) {
    const node = NODE_NAMES[r.arg0] || ('node '+r.arg0);
    return name + ' LED'+(r.arg1+1)+' on '+node;
  }
  if ([8,9].includes(r.action)) {
    const node = NODE_NAMES[r.arg0] || ('node '+r.arg0);
    return name + ' on ' + node;
  }
  if (r.action === 10) {
    const cmds = {1:'Lock',2:'Unlock',3:'Start'};
    return 'Viper ' + (cmds[r.arg0] || r.arg0);
  }
  return name;
}

let rulesData = [];
let editingIdx = -1;

async function loadRules() {
  try {
    const r = await fetch('/api/rules');
    rulesData = await r.json();
    renderRules();
  } catch(e) {
    document.getElementById('rules-panel').innerHTML = '<div style="color:#f29900;padding:14px">Failed to load rules.</div>';
  }
}

function renderRules() {
  const panel = document.getElementById('rules-panel');
  let h = '<div class="rules-toolbar">';
  h += '<button class="ctrl-btn" onclick="startEdit(-1)">+ Add Rule</button>';
  h += '<button class="ctrl-btn danger" onclick="resetRules()">Factory Reset</button>';
  h += '</div>';
  if (editingIdx === -2) h += buildEditor(null, -1);  // new rule form
  for (const r of rulesData) {
    const empty = !r.trig_id && !r.action;
    if (editingIdx === r.i) { h += buildEditor(r, r.i); continue; }
    h += `<div class="rule-row${empty?' empty':''}">`;
    h += `<div class="rule-idx">${r.i}</div>`;
    h += '<div class="rule-desc">';
    if (empty) {
      h += '<div class="rule-empty-text">empty slot</div>';
    } else {
      const trig = describeTrigger(r);
      const act  = describeAction(r);
      h += `<div class="rule-trig">&#x2139; ${trig||'?'}</div>`;
      h += `<div class="rule-act">&#x279C; ${act}</div>`;
    }
    h += '</div>';
    h += '<div class="rule-btns">';
    h += `<button class="rule-edit-btn" onclick="startEdit(${r.i})">Edit</button>`;
    if (!empty) h += `<button class="rule-del-btn" onclick="deleteRule(${r.i})">Clear</button>`;
    h += '</div></div>';
  }
  panel.innerHTML = h;
}

function buildEditor(r, idx) {
  const isNew = idx === -1;
  const slotIdx = isNew ? firstEmptySlot() : idx;
  const v = r || {trig_id:0,c0_byte:0,c0_val:0,c0_mask:0,c1_byte:0,c1_val:0,c1_mask:0,action:0,arg0:0,arg1:0,arg2:0};
  const trigOpts = Object.entries(CAN_NAMES).map(([id,name]) =>
    `<option value="${id}" ${v.trig_id==id?'selected':''}>${name} (0x${(+id).toString(16).toUpperCase()})</option>`
  ).join('');
  const actOpts = ACT_NAMES.map((n,i) =>
    `<option value="${i}" ${v.action==i?'selected':''}>${i}: ${n}</option>`
  ).join('');
  let h = `<div class="rule-editor">`;
  h += `<h4>${isNew ? 'New Rule → Slot '+slotIdx : 'Edit Rule '+idx}</h4>`;
  h += '<div class="re-row"><label>Trigger ID</label>';
  h += `<select id="re-trig"><option value="0">— custom —</option>${trigOpts}</select>`;
  h += `<input type="number" id="re-trig-raw" min="0" max="2047" value="${v.trig_id}" style="width:80px"> (raw)</div>`;
  h += '<div class="re-row"><label>Cond 0</label>';
  h += `byte <input type="number" id="re-c0b" min="0" max="7" value="${v.c0_byte}" style="width:50px">`;
  h += ` val <input type="number" id="re-c0v" min="0" max="255" value="${v.c0_val}" style="width:55px">`;
  h += ` mask <input type="number" id="re-c0m" min="0" max="255" value="${v.c0_mask}" style="width:55px">`;
  h += ' <span style="font-size:10px;color:#808a95">(0=skip)</span></div>';
  h += '<div class="re-row"><label>Cond 1</label>';
  h += `byte <input type="number" id="re-c1b" min="0" max="7" value="${v.c1_byte}" style="width:50px">`;
  h += ` val <input type="number" id="re-c1v" min="0" max="255" value="${v.c1_val}" style="width:55px">`;
  h += ` mask <input type="number" id="re-c1m" min="0" max="255" value="${v.c1_mask}" style="width:55px">`;
  h += ' <span style="font-size:10px;color:#808a95">(0=skip)</span></div>';
  h += '<div class="re-row"><label>Action</label>';
  h += `<select id="re-act">${actOpts}</select></div>`;
  h += '<div class="re-row"><label>arg0</label>';
  h += `<input type="number" id="re-a0" min="0" max="255" value="${v.arg0}" style="width:70px">`;
  h += ' <label style="margin-left:10px">arg1</label>';
  h += `<input type="number" id="re-a1" min="0" max="255" value="${v.arg1}" style="width:70px">`;
  h += ' <label style="margin-left:10px">arg2</label>';
  h += `<input type="number" id="re-a2" min="0" max="255" value="${v.arg2}" style="width:70px"></div>`;
  h += `<div class="re-row"><button class="re-save" onclick="saveRule(${slotIdx})">Save</button>`;
  h += `<button class="re-cancel" onclick="cancelEdit()" style="margin-left:6px">Cancel</button></div>`;
  h += '</div>';
  return h;
}

function firstEmptySlot() {
  for (const r of rulesData) if (!r.trig_id && !r.action) return r.i;
  return rulesData.length - 1;
}

function startEdit(idx) {
  editingIdx = (idx === -1) ? -2 : idx;
  renderRules();
}
function cancelEdit() { editingIdx = -1; renderRules(); }

function readEditorTrigId() {
  const sel = document.getElementById('re-trig');
  const raw = +document.getElementById('re-trig-raw').value;
  return (sel && +sel.value > 0) ? +sel.value : raw;
}

async function saveRule(idx) {
  const body = {
    i: idx,
    trig_id: readEditorTrigId(),
    c0_byte: +document.getElementById('re-c0b').value,
    c0_val:  +document.getElementById('re-c0v').value,
    c0_mask: +document.getElementById('re-c0m').value,
    c1_byte: +document.getElementById('re-c1b').value,
    c1_val:  +document.getElementById('re-c1v').value,
    c1_mask: +document.getElementById('re-c1m').value,
    action:  +document.getElementById('re-act').value,
    arg0:    +document.getElementById('re-a0').value,
    arg1:    +document.getElementById('re-a1').value,
    arg2:    +document.getElementById('re-a2').value,
  };
  try {
    await fetch('/api/rules', {
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body: JSON.stringify(body)
    });
    editingIdx = -1;
    await loadRules();
  } catch(e) { alert('Save failed: ' + e); }
}

async function deleteRule(idx) {
  try {
    await fetch('/api/rules?i=' + idx, { method:'DELETE' });
    await loadRules();
  } catch(e) { alert('Delete failed: ' + e); }
}

async function resetRules() {
  if (!confirm('Restore all rules to compiled defaults?')) return;
  try {
    await fetch('/api/rules/reset', { method:'POST' });
    await loadRules();
  } catch(e) { alert('Reset failed: ' + e); }
}

// ---------- events ----------
cmd.addEventListener('keydown', e => {
  if (e.key === 'Enter') { submit(); }
  else if (e.key === 'ArrowUp')   { if (histIdx > 0) { histIdx--; cmd.value = history[histIdx]; } }
  else if (e.key === 'ArrowDown') { if (histIdx < history.length - 1) { histIdx++; cmd.value = history[histIdx]; } else { histIdx = history.length; cmd.value = ''; } }
});
updateTxModeButtons(0);

initControl();
pollStatus(); setInterval(pollStatus, 1500);
pollFrames(); setInterval(pollFrames, 250);
pollSerial(); setInterval(pollSerial, 500);
</script>
</body>
</html>
)HTMLPAGE";
