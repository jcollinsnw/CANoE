// can_protocol.h
// Shared CAN message layout for the accessory bus.
// Copy this same file into every node's sketch folder so both sides agree.
//
// Bus: 125 kbit/s, standard 11-bit IDs. Low speed gives us noise margin in
// bench mode (open-drain + pull-up) and still runs fine through real
// transceivers in the car. Bump to 250 or 500 kbit/s later if you want.

#pragma once
#include <stdint.h>

// --------------------------------------------------------------
// Message IDs
// --------------------------------------------------------------
#define CAN_ID_RELAY_CMD         0x100  // Switch panel  -> Relay controller
#define CAN_ID_RELAY_STATUS      0x101  // Relay ctrl    -> everyone, periodic
#define CAN_ID_LED_CMD           0x102  // Any node -> target node: [target, mask, state]
#define CAN_ID_LED_STATUS        0x103  // Target node  -> everyone, on change: [node_id, bitmap]
#define CAN_ID_BUZZER_CMD        0x104  // Any node -> target node: [target, cmd, arg0]
                                        //   target: node_id or 0xFF (broadcast)
                                        //   cmd: BUZZER_SEQ_* or BUZZER_CMD_MUTE
                                        //   arg0: seq argument (peer count for BUZZER_SEQ_PEER;
                                        //         1/0 for BUZZER_CMD_MUTE)
#define CAN_ID_SWITCH_EVENT      0x200  // Switch panel  -> everyone, on change
#define CAN_ID_SWITCH_ACK        0x202  // Any non-originating node -> switch panel: [switch_id, event]
#define CAN_ID_ENCODER_EVENT     0x201  // Switch panel  -> everyone: rotary encoder
#define CAN_ID_TELEMETRY         0x300  // Relay ctrl    -> everyone, periodic
#define CAN_ID_ENV_DATA          0x301  // Switch panel  -> everyone, periodic (temp/humidity)
#define CAN_ID_IMU_DATA          0x302  // Viper iface   -> everyone, periodic (accel xyz)
#define CAN_ID_SHAKE_EVENT       0x303  // Viper iface   -> everyone, on detection
#define CAN_ID_ENGINE_DATA       0x304  // RPM node      -> everyone: [rpm_lo, rpm_hi]
#define CAN_ID_GPS_DATA          0x305  // GPS node      -> everyone: [speed_lo, speed_hi, heading_lo, heading_hi, flags]

#define CAN_ID_WBO2_DATA         0x306  // WBO2 node     -> everyone: [afr_lo, afr_hi] (AFR × 100)

#define CAN_ID_ECU_DATA          0x307  // ECU node      -> everyone, 250 ms:
                                        //   [0] mode (0=carb, 1=inject)
                                        //   [1] MAP kPa  (0–255)
                                        //   [2] TPS %    (0–100)
                                        //   [3] CLT °C + 40  (0 = −40°C)
                                        //   [4] IAT °C + 40
                                        //   [5] pw_lo — solenoid duty×100 or inj PW μs (uint16 LE)
                                        //   [6] pw_hi
                                        //   [7] flags: bit0=closed_loop, bit1=enriching,
                                        //              bit2=inj_saturated, bit3=running
#define CAN_ID_ECU_CMD           0x308  // Any node      -> ECU: [cmd, arg0, arg1, arg2]
                                        //   cmd 0x01: set mode   (arg0: 0=carb 1=inject)
                                        //   cmd 0x02: set target AFR×100 (arg1+arg2 = uint16 LE)
                                        //   cmd 0x03: fuel cut   (arg0: 0=off 1=on)
                                        //   cmd 0x04: reset fuel trim

// Node presence heartbeat — every node broadcasts this every 5 s.
// data[0]=node_id, data[1]=peer_count, data[2]=can_ok (0/1)
// The web UI uses data[0] to identify the sender so all nodes share one CAN ID.
#define CAN_ID_NODE_ANNOUNCE     0x0F0

// Boot event — emitted once at the end of setup(), self-echoed back into rx.
// data[0]=node_id. Use TRIG_BOOT() in RULES_DEFAULT_INIT to fire rules on startup.
#define CAN_ID_BOOT_EVENT        0x0F1

// Node capability advertisement — every node broadcasts at boot and on NODE_CAP_REQ.
// data[0]=node_id, data[1]=caps_bitmask, data[2]=switch_count, data[3]=button_count,
// data[4]=led_count, data[5]=relay_count
#define CAN_ID_NODE_CAP          0x0F2
// Node capability request — any node replies with CAN_ID_NODE_CAP.
// data[0]=target_node_id (0xFF = all nodes)
#define CAN_ID_NODE_CAP_REQ      0x0F3

// Reboot command — any node may send; target restarts when its node_id matches or target is 0xFF.
// data[0] = target_node_id (0x01–0x04 for a specific node, 0xFF = all nodes)
#define CAN_ID_REBOOT_CMD        0x0F5

// Bus error event — emitted by any node that detects a CAN error condition.
// Sent on both transports (ESP-NOW carries it even if wired CAN is failing).
// data[0]=node_id, data[1]=error_code (BUS_ERR_*), data[2]=tx_err_cnt, data[3]=rx_err_cnt
#define CAN_ID_BUS_ERROR         0x0F4

// Bus error codes for CAN_ID_BUS_ERROR data[1]
#define BUS_ERR_BUS_OFF          0x01  // TWAI entered bus-off state; recovery initiated
#define BUS_ERR_ERROR_PASSIVE    0x02  // TEC or REC >= 96 (error passive threshold)
#define BUS_ERR_TX_FAIL          0x03  // 5+ consecutive TX failures (no ACK)
#define BUS_ERR_RX_OVERFLOW      0x04  // RX buffer overrun — frames were silently dropped

// Capability bits for CAN_ID_NODE_CAP data[1]
#define NODE_CAP_RELAY    0x01   // has relay outputs
#define NODE_CAP_SWITCHES 0x02   // has switch/button inputs
#define NODE_CAP_VIPER    0x04   // has Viper alarm interface
#define NODE_CAP_LEDS     0x08   // has CAN-controllable LEDs
#define NODE_CAP_RULES    0x10   // has rules engine

// Generic chunked blob write protocol.
// One transfer = N BLOB_WRITE frames + one BLOB_COMMIT frame.
// Multiple (ns, key) transfers may interleave; each has its own RX slot.
// Self-echoed frames are ignored on the receiving side.
//
// BLOB_WRITE  [0]=target [1]=ns [2]=key [3]=chunk_idx [4-7]=data (4 bytes)
// BLOB_COMMIT [0]=target [1]=ns [2]=key [3]=len_lo    [4]=len_hi [5]=flags
#define CAN_ID_BLOB_WRITE    0x410
#define CAN_ID_BLOB_COMMIT   0x411

// Blob namespaces
#define BLOB_NS_WIFI         0x01   // WiFi + ESP-NOW credentials
#define BLOB_NS_RULES        0x02   // Rules engine: key = slot index (0..MAX_RULES-1),
                                    //   payload = 12-byte CanRule (zeroed = clear slot).
                                    //   key 0xFE = factory reset (any 1-byte payload).

// Keys within BLOB_NS_WIFI
#define BLOB_KEY_SSID        0x01   // AP SSID string, max 32 bytes
#define BLOB_KEY_PASS        0x02   // AP password string, max 63 bytes (0 len = open)
#define BLOB_KEY_PMK         0x03   // ESP-NOW PMK, exactly 16 bytes
#define BLOB_KEY_LMK         0x04   // ESP-NOW LMK, exactly 16 bytes

// Blob commit flags (BLOB_COMMIT data[5])
#define BLOB_FLAG_PERSIST    0x01   // write to NVS after commit
#define BLOB_FLAG_REBOOT     0x02   // restart node after applying

// Config-over-CAN — change behavior at runtime without reflashing.
#define CAN_ID_CONFIG_WRITE      0x400
#define CAN_ID_CONFIG_READ_REQ   0x401
#define CAN_ID_CONFIG_READ_RESP  0x402
#define CAN_ID_CONFIG_SAVE       0x403

// Peripheral commands.
#define CAN_ID_LCD_CMD           0x500  // Anyone -> switch_panel: write LCD text

// Viper alarm interface node (NODE_ID 0x03)
#define CAN_ID_VIPER_CMD         0x510  // Any node -> viper_interface: data[0] = VIPER_CMD_*
#define CAN_ID_VIPER_STATUS      0x511  // viper_interface -> everyone: data[0..4] = raw 5-byte alarm packet

// Viper command codes for CAN_ID_VIPER_CMD data[0]
#define VIPER_CMD_LOCK           0x01   // Arm / lock doors
#define VIPER_CMD_UNLOCK         0x02   // Disarm / unlock doors
#define VIPER_CMD_REMOTE_START   0x03   // Remote start engine

// Buzzer sequence/command codes for CAN_ID_BUZZER_CMD data[1]
#define BUZZER_SEQ_ALERT          0x01  // 3-beep urgent warning
#define BUZZER_SEQ_CAN_UP         0x02  // rising 3-tone: link restored
#define BUZZER_SEQ_CAN_DOWN       0x03  // 2 low pulses: link lost
#define BUZZER_SEQ_STARTUP        0x04  // boot jingle
#define BUZZER_SEQ_PEER           0x05  // peer-count tone; arg0 = count
#define BUZZER_SEQ_WIFI_CONNECT   0x06  // ascending 2-note chime
#define BUZZER_SEQ_WIFI_DISCONNECT 0x07 // descending 2-note chime
#define BUZZER_SEQ_RELAY_ON       0x10  // relay-on sound
#define BUZZER_SEQ_RELAY_OFF      0x11  // relay-off sound
#define BUZZER_SEQ_ALL_OFF        0x12  // all-relays-off sound
#define BUZZER_CMD_MUTE           0x20  // arg0: 1 = mute, 0 = unmute

#define CFG_TARGET_VIPER         0x03
#define CFG_TARGET_ECU           0x04

// --------------------------------------------------------------
// CAN_ID_RELAY_CMD (2 bytes)
//   data[0] : mask   — bit N set = "act on relay N"  (bit 0 = relay 1, ... bit 5 = relay 6)
//   data[1] : state  — bit N is the desired state of relay N (1 = on, 0 = off)
// Only relays whose mask bit is 1 are changed. Others keep their current state.
// --------------------------------------------------------------

// --------------------------------------------------------------
// CAN_ID_RELAY_STATUS  (1 byte)    — bit 0 = relay 1 on/off, etc. ~5 Hz.
// CAN_ID_SWITCH_EVENT  (2 bytes)   — [switch_id, SwitchEvent]. On change.
// CAN_ID_ENCODER_EVENT (2 bytes)   — [EncoderEvent, count]. On change.
// CAN_ID_TELEMETRY     (8 bytes)   — vbat_cv, ibatt_da, vsolar_cv, flags, _
// CAN_ID_LCD_CMD       (2-8 bytes) — [row, col, char...] or [0xFF] to clear.
// --------------------------------------------------------------

// --------------------------------------------------------------
// Switch events — published on every change.
// --------------------------------------------------------------
enum SwitchEvent : uint8_t {
  SW_RELEASE       = 0,
  SW_PRESS         = 1,
  SW_LONG_PRESS    = 2,
  SW_DOUBLE_PRESS  = 3,
};

// Encoder events — published on CAN_ID_ENCODER_EVENT.
// data[0] = EncoderEvent, data[1] = step count (rotation events only).
enum EncoderEvent : uint8_t {
  ENC_ROTATE_CW   = 0,  // clockwise detent(s); data[1] = count
  ENC_ROTATE_CCW  = 1,  // counter-clockwise;   data[1] = count
  ENC_PRESS       = 2,  // push-button pressed
  ENC_RELEASE     = 3,  // push-button released
  ENC_LONG_PRESS  = 4,  // held longer than LONG_PRESS_MS
};

// --------------------------------------------------------------
// Rules engine — any CAN frame → any CAN action, stored in NVS.
// Rules are evaluated on every received frame; the first matching
// condition fires its action. trig_id == 0 means the slot is empty.
//
// Condition matching (c0 and c1 are independent; c*_mask == 0 skips):
//   (frame.data[c*_byte] & c*_mask) == (c*_val & c*_mask)
// --------------------------------------------------------------
enum RuleActionKind : uint8_t {
  RULE_ACT_NONE         = 0,   // slot disabled
  RULE_ACT_RELAY_TOGGLE = 1,   // arg0 = relay idx (0-5)
  RULE_ACT_RELAY_ON     = 2,   // arg0 = relay idx
  RULE_ACT_RELAY_OFF    = 3,   // arg0 = relay idx
  RULE_ACT_ALL_OFF      = 4,   // all relays off
  RULE_ACT_RELAY_SCENE  = 5,   // arg0 = 6-bit relay bitmap
  RULE_ACT_LED_ON       = 6,   // arg0 = target node_id, arg1 = led index
  RULE_ACT_LED_OFF      = 7,   // arg0 = target node_id, arg1 = led index
  RULE_ACT_WIFI_ENABLE  = 8,   // arg0 = target node_id
  RULE_ACT_WIFI_DISABLE = 9,   // arg0 = target node_id
  RULE_ACT_VIPER_CMD    = 10,  // arg0 = VIPER_CMD_*
  RULE_ACT_MENU_SELECT      = 11,  // short-press menu select (no-op when menu closed)
  RULE_ACT_MENU_ENTER       = 12,  // long-press: enter menu or confirm action
  RULE_ACT_RELAY_TIMED_OFF  = 13,  // arg0 = relay idx (0-5), arg1 = seconds (1-255)
  RULE_ACT_LED_FLASH        = 14,  // arg0 = target node_id, arg1 = led idx, arg2 = period (×50ms half-period)
  RULE_ACT_BUZZER_ALERT     = 15,  // play a 3-beep warning on the local buzzer (no-op without ENABLE_BUZZER)
  RULE_ACT_BUZZER_PLAY      = 16,  // send BUZZER_CMD: arg0=target, arg1=BUZZER_SEQ_*, arg2=seq_arg
};

struct CanRule {
  uint16_t trig_id;   // CAN frame ID to watch; 0 = slot disabled
  uint8_t  c0_byte;   // data byte index for condition 0
  uint8_t  c0_val;    // expected value (after masking)
  uint8_t  c0_mask;   // AND mask; 0x00 = skip this condition entirely
  uint8_t  c1_byte;   // data byte index for condition 1
  uint8_t  c1_val;
  uint8_t  c1_mask;   // 0x00 = skip
  uint8_t  action;    // RuleActionKind
  uint8_t  arg0;
  uint8_t  arg1;
  uint8_t  arg2;
};  // 12 bytes — fits cleanly in NVS putBytes

// --------------------------------------------------------------
// Trigger macros — expand to the CanRule trigger fields:
//   trig_id, c0_byte, c0_val, c0_mask, c1_byte, c1_val, c1_mask
// --------------------------------------------------------------
// Switch events (SWITCH_EVENT: data[0]=sw_idx, data[1]=SwitchEvent)
#define TRIG_SW_PRESS(idx)    CAN_ID_SWITCH_EVENT, 0, (idx), 0xFF, 1, SW_PRESS,      0xFF
#define TRIG_SW_RELEASE(idx)  CAN_ID_SWITCH_EVENT, 0, (idx), 0xFF, 1, SW_RELEASE,    0xFF
#define TRIG_SW_LONG(idx)     CAN_ID_SWITCH_EVENT, 0, (idx), 0xFF, 1, SW_LONG_PRESS, 0xFF

// Relay status (RELAY_STATUS: data[0] = relay bitmap at 5 Hz)
// These fire every status broadcast while the condition holds.
#define TRIG_RELAY_BIT_ON(n)  CAN_ID_RELAY_STATUS, 0, (1<<(n)), (1<<(n)), 0, 0, 0x00
#define TRIG_RELAY_BIT_OFF(n) CAN_ID_RELAY_STATUS, 0, 0x00,     (1<<(n)), 0, 0, 0x00

// Relay command (RELAY_CMD: data[0]=mask, data[1]=state — fires only on changes)
#define TRIG_RELAY_CMD_ON(n)  CAN_ID_RELAY_CMD, 0, (1<<(n)), (1<<(n)), 1, (1<<(n)), (1<<(n))
#define TRIG_RELAY_CMD_OFF(n) CAN_ID_RELAY_CMD, 0, (1<<(n)), (1<<(n)), 1, 0x00,     (1<<(n))

// Match any frame with a given ID (no byte conditions)
#define TRIG_ANY(id)          (id), 0, 0, 0x00, 0, 0, 0x00

// Boot event — fires once at startup on the node that emitted it (self-echo).
// data[0] = node_id; matches only this node's own boot event.
#define TRIG_BOOT()           CAN_ID_BOOT_EVENT, 0, NODE_ID, 0xFF, 0, 0, 0x00

// Bus error — fires when any node broadcasts CAN_ID_BUS_ERROR (any error code).
#define TRIG_BUS_ERROR()      CAN_ID_BUS_ERROR, 0, 0, 0x00, 0, 0, 0x00

// CAN OK — fires when a NODE_ANNOUNCE with can_ok=1 arrives; use to auto-clear error indicators.
// data[2] = can_ok (1 = bus healthy); matches any node announcing itself as healthy.
#define TRIG_CAN_OK()         CAN_ID_NODE_ANNOUNCE, 2, 0x01, 0xFF, 0, 0, 0x00

// --------------------------------------------------------------
// Action macros — expand to the CanRule action fields:
//   action, arg0, arg1, arg2
// --------------------------------------------------------------
#define ACT_RELAY_TOGGLE(r)    RULE_ACT_RELAY_TOGGLE, (r),    0,      0
#define ACT_RELAY_ON(r)        RULE_ACT_RELAY_ON,     (r),    0,      0
#define ACT_RELAY_OFF(r)       RULE_ACT_RELAY_OFF,    (r),    0,      0
#define ACT_ALL_OFF()          RULE_ACT_ALL_OFF,      0,      0,      0
#define ACT_RELAY_SCENE(bmap)  RULE_ACT_RELAY_SCENE,  (bmap), 0,      0
#define ACT_LED_ON(node, led)  RULE_ACT_LED_ON,       (node), (led),  0
#define ACT_LED_OFF(node, led) RULE_ACT_LED_OFF,      (node), (led),  0
#define ACT_WIFI_ENABLE(node)  RULE_ACT_WIFI_ENABLE,  (node), 0,      0
#define ACT_WIFI_DISABLE(node) RULE_ACT_WIFI_DISABLE, (node), 0,      0
#define ACT_VIPER(cmd)         RULE_ACT_VIPER_CMD,    (cmd),  0,      0
#define ACT_MENU_SELECT()             RULE_ACT_MENU_SELECT,     0,     0, 0
#define ACT_MENU_ENTER()              RULE_ACT_MENU_ENTER,      0,     0, 0
#define ACT_RELAY_TIMED_OFF(r, sec)   RULE_ACT_RELAY_TIMED_OFF, (r), (sec), 0
#define ACT_LED_FLASH(node, led, per) RULE_ACT_LED_FLASH,        (node), (led), (per)
#define ACT_BUZZER_ALERT()                    RULE_ACT_BUZZER_ALERT, 0, 0, 0
#define ACT_BUZZER_PLAY(target, seq)          RULE_ACT_BUZZER_PLAY, (target), (seq), 0
#define ACT_BUZZER_PLAY_ARG(target, seq, arg) RULE_ACT_BUZZER_PLAY, (target), (seq), (arg)

// Convenience: wrap a trigger + action pair into a CanRule initialiser.
// Usage: RULE(TRIG_SW_PRESS(0), ACT_RELAY_TOGGLE(0))
#define RULE(trig, act)  { trig, act }

// --------------------------------------------------------------
// CONFIG PROTOCOL
// Every config frame carries an 8-bit `target` so you can address a
// specific node type, or broadcast to all.
// --------------------------------------------------------------
#define CFG_TARGET_SWITCH_PANEL   0x01
#define CFG_TARGET_RELAY_CTRL     0x02
#define CFG_TARGET_BROADCAST      0xFF

// Keys — what piece of config is being addressed.
//   Relay controller side:
#define CFG_KEY_NODE_ID           0x01   // data[4] = new node_id (0x01-0xFE); node restarts. Broadcast target NOT accepted.
#define CFG_KEY_RELAY_MAX_ON_MS   0x20   // per-relay safety auto-off (0 = no limit)
#define CFG_KEY_RPM_REDLINE       0x40   // RPM redline for display widget; arg2_lo/hi = RPM uint16
#define CFG_KEY_ECU_MODE          0x51   // 0=carb, 1=inject; arg[4]=value, flags bit0=persist
#define CFG_KEY_ECU_TARGET_AFR    0x52   // target AFR × 100; arg2_lo/hi = uint16
#define CFG_KEY_ECU_BASE_PW       0x53   // base pulse width μs at 100% VE, 100 kPa; arg2_lo/hi
//   Any node:
#define CFG_KEY_WIFI_ENABLED      0x30   // legacy: sets both ap_en and espnow_en; node restarts
#define CFG_KEY_AP_ENABLED        0x31   // data[4]=0/1 — enable/disable SoftAP + web server; restarts
#define CFG_KEY_ESPNOW_ENABLED    0x32   // data[4]=0/1 — enable/disable ESP-NOW radio; restarts

// Save actions (for CAN_ID_CONFIG_SAVE data[1]):
#define CFG_SAVE_COMMIT           0x01   // flush RAM config to NVS
#define CFG_SAVE_RELOAD           0x02   // drop RAM changes, reload from NVS
#define CFG_SAVE_FACTORY_RESET    0x03   // clear NVS, revert to compiled defaults

// CAN_ID_CONFIG_WRITE (8 bytes)
//   [0] target
//   [1] key
//   [2] index     (switch id, relay id, etc.)
//   [3] kind      (unused / reserved)
//   [4] arg
//   [5] arg2_lo
//   [6] arg2_hi
//   [7] flags     (bit 0 = persist to NVS immediately after applying)
//
// CAN_ID_CONFIG_READ_REQ (3 bytes)
//   [0] target
//   [1] key
//   [2] index     (0xFF = request all indices for this key)
//
// CAN_ID_CONFIG_READ_RESP (8 bytes)
//   Same layout as CAN_ID_CONFIG_WRITE (flags byte unused).
//
// CAN_ID_CONFIG_SAVE (2 bytes)
//   [0] target
//   [1] action    (CFG_SAVE_* above)
// --------------------------------------------------------------

// Little-endian int16 pack/unpack helpers
static inline void pack_i16(uint8_t *buf, int16_t v) {
  buf[0] = (uint8_t)(v & 0xFF);
  buf[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline int16_t unpack_i16(const uint8_t *buf) {
  return (int16_t)(buf[0] | (buf[1] << 8));
}
static inline void pack_u16(uint8_t *buf, uint16_t v) {
  buf[0] = (uint8_t)(v & 0xFF);
  buf[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline uint16_t unpack_u16(const uint8_t *buf) {
  return (uint16_t)(buf[0] | (buf[1] << 8));
}
