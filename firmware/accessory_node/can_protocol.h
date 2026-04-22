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
#define CAN_ID_SWITCH_EVENT      0x200  // Switch panel  -> everyone, on change
#define CAN_ID_ENCODER_EVENT     0x201  // Switch panel  -> everyone: rotary encoder
#define CAN_ID_TELEMETRY         0x300  // Relay ctrl    -> everyone, periodic
#define CAN_ID_ENV_DATA          0x301  // Switch panel  -> everyone, periodic (temp/humidity)
#define CAN_ID_IMU_DATA          0x302  // Viper iface   -> everyone, periodic (accel xyz)
#define CAN_ID_SHAKE_EVENT       0x303  // Viper iface   -> everyone, on detection

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

#define CFG_TARGET_VIPER         0x03

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
// Switch action kinds — what happens when a switch is pressed.
// The switch panel stores one of these per switch and executes it
// locally (sending RELAY_CMD frames as needed).
// --------------------------------------------------------------
enum SwitchActionKind : uint8_t {
  SW_ACT_TOGGLE     = 0,  // arg = relay idx. Press toggles that relay.
  SW_ACT_PULSE      = 1,  // arg = relay idx, arg2 = pulse length in ms.
  SW_ACT_EVENT_ONLY = 2,  // Publish SW_PRESS only; no relay change.
  SW_ACT_HOLD       = 3,  // arg = relay idx. Relay ON while held, OFF on release. (Horn-style.)
  SW_ACT_SCENE      = 4,  // arg = 6-bit relay bitmap. Press sets mask=0x3F, state=arg.
};

// The in-RAM switch mapping record. The switch panel keeps an array
// of these, one per switch. Size is fixed at 4 bytes so it fits cleanly
// in a CAN payload and in NVS.
struct SwitchAction {
  uint8_t  kind;   // SwitchActionKind
  uint8_t  arg;    // relay idx, bitmap, etc (depends on kind)
  uint16_t arg2;   // extra param (pulse ms, etc)
};

// --------------------------------------------------------------
// CONFIG PROTOCOL
// Every config frame carries an 8-bit `target` so you can address a
// specific node type, or broadcast to all.
// --------------------------------------------------------------
#define CFG_TARGET_SWITCH_PANEL   0x01
#define CFG_TARGET_RELAY_CTRL     0x02
#define CFG_TARGET_BROADCAST      0xFF

// Keys — what piece of config is being addressed.
//   Switch panel side:
#define CFG_KEY_SW_ACTION         0x10   // per-switch SwitchAction record
//   Relay controller side:
#define CFG_KEY_RELAY_MAX_ON_MS   0x20   // per-relay safety auto-off (0 = no limit)
//   Any node:
#define CFG_KEY_WIFI_ENABLED      0x30   // data[4]=0 disable / 1 enable; node restarts to apply

// Save actions (for CAN_ID_CONFIG_SAVE data[1]):
#define CFG_SAVE_COMMIT           0x01   // flush RAM config to NVS
#define CFG_SAVE_RELOAD           0x02   // drop RAM changes, reload from NVS
#define CFG_SAVE_FACTORY_RESET    0x03   // clear NVS, revert to compiled defaults

// CAN_ID_CONFIG_WRITE (8 bytes)
//   [0] target
//   [1] key
//   [2] index     (switch id, relay id, etc.)
//   [3] kind      (for CFG_KEY_SW_ACTION: SwitchActionKind; else unused)
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
