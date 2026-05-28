// mod_error.cpp — unified error event system.
//
// Emits CAN_ID_ERROR_EVENT (0x0F6) frames and reacts to incoming ones by
// driving the buzzer, LEDs, and Cardputer speaker based on severity.
// No feature flag gate — always compiled in (tiny footprint, critical safety path).

#include <Arduino.h>
#include "mod_error.h"
#include "can_protocol.h"
#include "bus.h"
#include "mod_buzzer.h"
#include "mod_led.h"
#include "mod_m5_cardputer.h"

#if USE_WIFI
#include "webui.h"
#else
#define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#define wlogln(msg)     Serial.println(msg)
#endif

// Active error table — tracks currently active faults for display/query.
static ActiveError g_errors[ERR_MAX_ACTIVE];
static uint8_t g_error_count = 0;

// Rate limiting: don't spam the same error code more than once per second.
static uint8_t  g_last_code = 0;
static uint32_t g_last_raise_ms = 0;
static const uint32_t ERR_RATE_LIMIT_MS = 1000;

// -------------------------------------------------------------------
// Emitter
// -------------------------------------------------------------------

void error_raise(uint8_t error_code, uint8_t severity, uint8_t target,
                 uint8_t flags, uint8_t arg0, uint8_t arg1, uint8_t arg2) {
    // Rate limit: suppress duplicate codes within 1 s
    uint32_t now = millis();
    if (error_code == g_last_code && (now - g_last_raise_ms) < ERR_RATE_LIMIT_MS) return;
    g_last_code = error_code;
    g_last_raise_ms = now;

    uint8_t d[8] = {
        bus_node_id(),   // data[0]: source
        error_code,      // data[1]: ERR_*
        severity,        // data[2]: ERR_SEV_*
        target,          // data[3]: target node or 0xFF
        flags,           // data[4]: ERR_FLAG_*
        arg0,            // data[5]
        arg1,            // data[6]
        arg2             // data[7]
    };
    bus_tx(CAN_ID_ERROR_EVENT, d, 8);
}

void error_clear(uint8_t error_code) {
    uint8_t d[8] = {
        bus_node_id(),
        error_code,
        ERR_SEV_INFO,     // severity irrelevant on clear
        0xFF,             // broadcast clear
        0,                // flags: ACTIVE bit = 0 → cleared
        0, 0, 0
    };
    bus_tx(CAN_ID_ERROR_EVENT, d, 8);
}

// -------------------------------------------------------------------
// Receiver — active error table management
// -------------------------------------------------------------------

static int find_error(uint8_t source, uint8_t code) {
    for (uint8_t i = 0; i < g_error_count; i++) {
        if (g_errors[i].source_node == source && g_errors[i].error_code == code)
            return i;
    }
    return -1;
}

static void remove_error(uint8_t idx) {
    if (idx >= g_error_count) return;
    g_error_count--;
    if (idx < g_error_count)
        g_errors[idx] = g_errors[g_error_count];
}

static void add_error(uint8_t source, uint8_t code, uint8_t severity, uint8_t flags) {
    int existing = find_error(source, code);
    if (existing >= 0) {
        // Update severity/flags if re-raised
        g_errors[existing].severity = severity;
        g_errors[existing].flags = flags;
        g_errors[existing].raised_at = millis();
        return;
    }
    if (g_error_count >= ERR_MAX_ACTIVE) {
        // Evict oldest non-latching error, or oldest overall
        uint8_t evict = 0;
        for (uint8_t i = 1; i < g_error_count; i++) {
            if (!(g_errors[i].flags & ERR_FLAG_LATCHING) &&
                (g_errors[evict].flags & ERR_FLAG_LATCHING ||
                 g_errors[i].raised_at < g_errors[evict].raised_at))
                evict = i;
        }
        remove_error(evict);
    }
    ActiveError& e = g_errors[g_error_count++];
    e.source_node = source;
    e.error_code = code;
    e.severity = severity;
    e.flags = flags;
    e.raised_at = millis();
}

// -------------------------------------------------------------------
// Receiver — react to ERROR_EVENT frames
// -------------------------------------------------------------------

void error_handle_frame(const BusFrame& f) {
    if (f.id != CAN_ID_ERROR_EVENT || f.dlc < 5) return;

    uint8_t source   = f.data[0];
    uint8_t code     = f.data[1];
    uint8_t severity = f.data[2];
    uint8_t target   = f.data[3];
    uint8_t flags    = f.data[4];

    // Target filtering: ignore if addressed to another specific node
    if (target != 0xFF && target != bus_node_id()) return;

    // Clear event
    if (!(flags & ERR_FLAG_ACTIVE)) {
        int idx = find_error(source, code);
        if (idx >= 0) remove_error(idx);
        // TODO: could turn off latched LED here
        return;
    }

    // Active error — add to table
    add_error(source, code, severity, flags);

    // Log to serial
    wlog("[error] %s sev=%u src=0x%02X arg=(%u,%u,%u)\n",
         error_code_name(code), severity, source,
         f.dlc >= 6 ? f.data[5] : 0,
         f.dlc >= 7 ? f.data[6] : 0,
         f.dlc >= 8 ? f.data[7] : 0);

    // --- Audible reaction (buzzer / Cardputer speaker) ---
    if (flags & ERR_FLAG_AUDIBLE) {
        switch (severity) {
            case ERR_SEV_INFO:
                // Silent — info errors don't buzz
                break;
            case ERR_SEV_WARNING:
                // Single short beep — distinguish from the urgent 3-pulse alert
                buzzer_alert();   // TODO: could add a softer single-beep variant
                m5_beep_alert();
                break;
            case ERR_SEV_CRITICAL:
                buzzer_alert();
                m5_beep_alert();
                break;
            case ERR_SEV_EMERGENCY:
                buzzer_fuel_pump_off();  // loudest multi-pulse alarm
                m5_beep_alert();
                break;
        }
    }

    // --- Visual reaction (LEDs) ---
    // Flash LED index 2 (the error indicator) on nodes that have LEDs.
    // CRITICAL/EMERGENCY = fast flash; WARNING = slow flash.
    if (flags & ERR_FLAG_VISUAL) {
#ifdef ENABLE_LEDS
        uint8_t period = (severity >= ERR_SEV_CRITICAL) ? 2 : 6;  // ×50ms half-period
        uint8_t led_d[4] = { bus_node_id(), 0x04, 0x04, period }; // mask=bit2, state=bit2, flash
        bus_tx(CAN_ID_LED_CMD, led_d, 4);
#endif
    }

    // --- Cardputer FEED / CLI bar event ---
    {
        char msg[24];
        snprintf(msg, sizeof(msg), "ERR: %s", error_code_name(code));
        m5_set_event(msg);
    }
}

// -------------------------------------------------------------------
// Tick — auto-clear expired non-latching errors
// -------------------------------------------------------------------

void error_tick() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < g_error_count; ) {
        ActiveError& e = g_errors[i];
        if (!(e.flags & ERR_FLAG_LATCHING) && (now - e.raised_at) >= ERR_AUTO_CLEAR_MS) {
            remove_error(i);
            // Don't increment i — slot was filled from the end
        } else {
            i++;
        }
    }
}

// -------------------------------------------------------------------
// Query API
// -------------------------------------------------------------------

bool error_any_active(uint8_t min_severity) {
    for (uint8_t i = 0; i < g_error_count; i++) {
        if (g_errors[i].severity >= min_severity) return true;
    }
    return false;
}

bool error_is_active(uint8_t error_code) {
    for (uint8_t i = 0; i < g_error_count; i++) {
        if (g_errors[i].error_code == error_code) return true;
    }
    return false;
}

uint8_t error_max_severity() {
    if (g_error_count == 0) return 0xFF;
    uint8_t max_sev = 0;
    for (uint8_t i = 0; i < g_error_count; i++) {
        if (g_errors[i].severity > max_sev) max_sev = g_errors[i].severity;
    }
    return max_sev;
}

// -------------------------------------------------------------------
// Human-readable error code names (short, for LCD/FEED)
// -------------------------------------------------------------------

const char* error_code_name(uint8_t code) {
    switch (code) {
        case ERR_CAN_INIT_FAIL:         return "CAN_INIT";
        case ERR_CAN_BUS_OFF:           return "BUS_OFF";
        case ERR_CAN_ERROR_PASSIVE:     return "ERR_PASSIVE";
        case ERR_CAN_TX_FAIL:           return "TX_FAIL";
        case ERR_CAN_RX_OVERFLOW:       return "RX_OVERFLOW";
        case ERR_ESPNOW_INIT_FAIL:      return "ESPNOW_INIT";
        case ERR_LOW_VOLTAGE:           return "LOW_BATT";
        case ERR_RELAY_WATCHDOG:        return "WDG_CUTOFF";
        case ERR_FUEL_PUMP_STALL:       return "PUMP_STALL";
        case ERR_INJECTOR_SATURATED:    return "INJ_SAT";
        case ERR_WBO2_SENSOR_FAULT:     return "WBO2_FAULT";
        case ERR_ECU_SENSOR_FAULT:      return "ECU_SENSOR";
        case ERR_MPU6050_FAIL:          return "IMU_FAIL";
        case ERR_DHT22_FAIL:            return "DHT_FAIL";
        case ERR_GPS_NO_FIX:            return "GPS_NOFIX";
        case ERR_SWITCH_ACK_TIMEOUT:    return "SW_ACK_TMO";
        case ERR_RELAY_CONFIRM_TIMEOUT: return "RLY_CONF_TMO";
        case ERR_WIFI_CREDS_REJECTED:   return "CRED_REJECT";
        case ERR_OTA_FAILED:            return "OTA_FAIL";
        case ERR_MQTT_CONNECT_FAIL:     return "MQTT_FAIL";
        case ERR_NVS_CORRUPT:           return "NVS_CORRUPT";
        default:                        return "UNKNOWN";
    }
}
