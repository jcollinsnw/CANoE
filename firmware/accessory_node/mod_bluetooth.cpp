// mod_bluetooth.cpp — BLE GATT CAN bus mirror.
//
// Exposes one GATT service with two characteristics:
//   TX (notify):  notifies the connected phone of every CAN frame seen by this node.
//   RX (write):   phone writes frames which are injected into the local bus via bus_tx().
//
// Frame wire format (11 bytes, fixed length): [id_lo, id_hi, dlc, d0..d7]
// Unused data bytes beyond dlc are zero-padded.

#include <Arduino.h>
#include "node_config.h"

#ifdef ENABLE_BLUETOOTH

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include <BLE2902.h>
#include <Preferences.h>

#include "can_protocol.h"
#include "bus.h"
#include "mod_bluetooth.h"

#if USE_WIFI
#  include "webui.h"
#else
#  define wlog(fmt, ...)  Serial.printf(fmt, ##__VA_ARGS__)
#  define wlogln(msg)     Serial.println(msg)
#endif

// ── ESP32→phone notify ring buffer (main task only — no locking needed) ───
#define BLE_TX_RING 32
static uint8_t s_tx_buf[BLE_TX_RING][BLE_FRAME_LEN];
static uint8_t s_tx_head = 0, s_tx_tail = 0;

static inline bool tx_empty() { return s_tx_head == s_tx_tail; }
static inline bool tx_full()  { return ((s_tx_tail + 1) % BLE_TX_RING) == s_tx_head; }

static void tx_push(const uint8_t* frame) {
    if (tx_full()) s_tx_head = (s_tx_head + 1) % BLE_TX_RING;
    memcpy(s_tx_buf[s_tx_tail], frame, BLE_FRAME_LEN);
    s_tx_tail = (s_tx_tail + 1) % BLE_TX_RING;
}

// ── Phone→ESP32 write ring buffer (shared: BT task writes, main reads) ────
#define BLE_RX_RING 8
static uint8_t          s_rx_buf[BLE_RX_RING][BLE_FRAME_LEN];
static volatile uint8_t s_rx_head = 0, s_rx_tail = 0;
static portMUX_TYPE     s_rx_mux  = portMUX_INITIALIZER_UNLOCKED;

static inline bool rx_empty_unsafe() { return s_rx_head == s_rx_tail; }
static inline bool rx_full_unsafe()  { return ((s_rx_tail + 1) % BLE_RX_RING) == s_rx_head; }

// ── BLE handles ───────────────────────────────────────────────────────────
static BLEServer*         s_server     = nullptr;
static BLECharacteristic* s_tx_char    = nullptr;
static BLECharacteristic* s_rx_char    = nullptr;
static bool               s_connected  = false;
static bool               s_advertising = false;

// ── Callbacks ─────────────────────────────────────────────────────────────
class BtServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        s_connected = true;
        wlogln("[bt] phone connected");
    }
    void onDisconnect(BLEServer*) override {
        s_connected = false;
        if (s_advertising) {
            wlogln("[bt] phone disconnected — restarting advertising");
            BLEDevice::startAdvertising();
        } else {
            wlogln("[bt] phone disconnected — advertising suppressed");
        }
    }
#if defined(CONFIG_BLUEDROID_ENABLED)
    void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t*) override    { onConnect(s); }
    void onDisconnect(BLEServer* s, esp_ble_gatts_cb_param_t*) override { onDisconnect(s); }
    void onMtuChanged(BLEServer*, esp_ble_gatts_cb_param_t*) override   {}
    void onConnParamsUpdate(esp_bd_addr_t, uint16_t, uint16_t, uint16_t, esp_bt_status_t) override {}
#endif
};

class BtRxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        String v = c->getValue();
        if ((int)v.length() < BLE_FRAME_LEN) return;
        portENTER_CRITICAL(&s_rx_mux);
        if (!rx_full_unsafe()) {
            memcpy(s_rx_buf[s_rx_tail], (const uint8_t*)v.c_str(), BLE_FRAME_LEN);
            s_rx_tail = (s_rx_tail + 1) % BLE_RX_RING;
        }
        portEXIT_CRITICAL(&s_rx_mux);
    }
#if defined(CONFIG_BLUEDROID_ENABLED)
    void onRead(BLECharacteristic*, esp_ble_gatts_cb_param_t*) override {}
    void onWrite(BLECharacteristic* c, esp_ble_gatts_cb_param_t*) override { onWrite(c); }
#endif
};

// ── Public API ────────────────────────────────────────────────────────────
void bluetooth_setup() {
    {
        Preferences p; p.begin(NVS_NAMESPACE, true);
        bool en = p.getBool("bt_en", true);
        p.end();
        if (!en) {
            wlogln("[bt] disabled by config");
            return;
        }
    }
#ifndef BLE_DEVICE_NAME
#  define BLE_DEVICE_NAME NODE_NAME
#endif
    BLEDevice::init(BLE_DEVICE_NAME);
    s_server = BLEDevice::createServer();
    s_server->setCallbacks(new BtServerCallbacks());

    BLEService* svc = s_server->createService(BLE_SERVICE_UUID);

    s_tx_char = svc->createCharacteristic(
        BLE_TX_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    s_tx_char->addDescriptor(new BLE2902());

    s_rx_char = svc->createCharacteristic(
        BLE_RX_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    s_rx_char->setCallbacks(new BtRxCallbacks());

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();
    s_advertising = true;

    wlog("[bt] advertising as \"%s\"\n", BLE_DEVICE_NAME);
}

bool bluetooth_is_advertising() { return s_advertising; }

void bluetooth_set_advertising(bool en) {
    s_advertising = en;
    if (en) {
        BLEDevice::startAdvertising();
        wlogln("[bt] advertising started");
    } else {
        BLEDevice::stopAdvertising();
        wlogln("[bt] advertising stopped");
    }
}

void bluetooth_loop() {
    while (true) {
        uint8_t frame[BLE_FRAME_LEN];
        bool got = false;
        portENTER_CRITICAL(&s_rx_mux);
        if (!rx_empty_unsafe()) {
            memcpy(frame, s_rx_buf[s_rx_head], BLE_FRAME_LEN);
            s_rx_head = (s_rx_head + 1) % BLE_RX_RING;
            got = true;
        }
        portEXIT_CRITICAL(&s_rx_mux);
        if (!got) break;
        uint16_t id  = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
        uint8_t  dlc = (frame[2] <= 8) ? frame[2] : 8;
        bus_tx(id, &frame[3], dlc);
    }

    if (!s_connected || !s_tx_char) return;
    while (!tx_empty()) {
        s_tx_char->setValue(s_tx_buf[s_tx_head], BLE_FRAME_LEN);
        s_tx_char->notify();
        s_tx_head = (s_tx_head + 1) % BLE_TX_RING;
    }
}

void bluetooth_handle_frame(const BusFrame& f) {
    // Forward every frame to any connected phone.
    uint8_t frame[BLE_FRAME_LEN] = {};
    frame[0] = (uint8_t)(f.id & 0xFF);
    frame[1] = (uint8_t)(f.id >> 8);
    frame[2] = (f.dlc <= 8) ? f.dlc : 8;
    memcpy(&frame[3], f.data, frame[2]);
    tx_push(frame);

    // Handle BT config commands (CONFIG_WRITE targeting this node or broadcast).
    if (f.id == CAN_ID_CONFIG_WRITE && f.dlc >= 5 &&
        (f.data[0] == NODE_ID || f.data[0] == CFG_TARGET_BROADCAST)) {
        uint8_t key = f.data[1];
        if (key == CFG_KEY_BT_ENABLED) {
            bool en = (f.data[4] != 0);
            Preferences p; p.begin(NVS_NAMESPACE, false);
            p.putBool("bt_en", en);
            p.end();
            wlog("[bt] bt_en=%u -> restart\n", en);
            delay(100); ESP.restart();
        } else if (key == CFG_KEY_BT_ADVERTISING) {
            bluetooth_set_advertising(f.data[4] != 0);
        }
    }
}

#endif  // ENABLE_BLUETOOTH
