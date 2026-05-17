#include "mod_blob.h"
#include "can_protocol.h"
#include "bus.h"
#include <string.h>

static BlobCommitCb g_cb = nullptr;

#define BLOB_RX_SLOTS 4
struct BlobRxSlot {
  uint8_t  ns, key;
  uint8_t  buf[BLOB_BUF_SIZE];
  bool     active;
};
static BlobRxSlot g_slots[BLOB_RX_SLOTS];

static BlobRxSlot* find_slot(uint8_t ns, uint8_t key, bool create) {
  for (int i = 0; i < BLOB_RX_SLOTS; i++)
    if (g_slots[i].active && g_slots[i].ns == ns && g_slots[i].key == key)
      return &g_slots[i];
  if (!create) return nullptr;
  for (int i = 0; i < BLOB_RX_SLOTS; i++) {
    if (!g_slots[i].active) {
      g_slots[i].ns = ns; g_slots[i].key = key;
      memset(g_slots[i].buf, 0, BLOB_BUF_SIZE);
      g_slots[i].active = true;
      return &g_slots[i];
    }
  }
  // Evict slot 0 if all full
  g_slots[0].ns = ns; g_slots[0].key = key;
  memset(g_slots[0].buf, 0, BLOB_BUF_SIZE);
  g_slots[0].active = true;
  return &g_slots[0];
}

void blob_set_commit_cb(BlobCommitCb cb) { g_cb = cb; }

void blob_handle_frame(const BusFrame& f) {
  if (strcmp(f.source, "self") == 0) return;  // sender handles itself directly

  if (f.id == CAN_ID_BLOB_WRITE && f.dlc >= 8) {
    if (f.data[0] != bus_node_id() && f.data[0] != 0xFF) return;
    uint8_t ns = f.data[1], key = f.data[2], idx = f.data[3];
    BlobRxSlot* s = find_slot(ns, key, true);
    uint16_t off = (uint16_t)idx * 4;
    if (off < BLOB_BUF_SIZE) {
      uint8_t n = (BLOB_BUF_SIZE - off >= 4) ? 4 : (uint8_t)(BLOB_BUF_SIZE - off);
      memcpy(s->buf + off, f.data + 4, n);
    }
  } else if (f.id == CAN_ID_BLOB_COMMIT && f.dlc >= 6) {
    if (f.data[0] != bus_node_id() && f.data[0] != 0xFF) return;
    uint8_t ns = f.data[1], key = f.data[2];
    uint16_t len = (uint16_t)f.data[3] | ((uint16_t)f.data[4] << 8);
    uint8_t flags = f.data[5];
    BlobRxSlot* s = find_slot(ns, key, false);
    if (s && len <= BLOB_BUF_SIZE) {
      if (g_cb) g_cb(ns, key, s->buf, len, flags);
      s->active = false;
    }
  }
}

void blob_send(uint8_t target, uint8_t ns, uint8_t key,
               const uint8_t* data, uint16_t len, uint8_t flags) {
  uint8_t chunks = (uint8_t)((len + 3) / 4);
  for (uint8_t i = 0; i < chunks; i++) {
    uint8_t d[8] = { target, ns, key, i, 0, 0, 0, 0 };
    uint16_t off = (uint16_t)i * 4;
    uint8_t n = (len - off >= 4) ? 4 : (uint8_t)(len - off);
    memcpy(d + 4, data + off, n);
    bus_tx(CAN_ID_BLOB_WRITE, d, 8);
  }
  uint8_t commit[6] = { target, ns, key, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8), flags };
  bus_tx(CAN_ID_BLOB_COMMIT, commit, 6);
}
