// mod_blob.h — generic chunked blob write protocol over CAN.
//
// A transfer is a sequence of BLOB_WRITE frames followed by BLOB_COMMIT.
// Transfers for different (ns, key) pairs interleave safely (4 RX slots).
// Self-echoed frames are ignored; the sender handles its own data directly.
//
// Namespace and key constants defined in can_protocol.h (BLOB_NS_*, BLOB_KEY_*).
// Commit flags: BLOB_FLAG_PERSIST, BLOB_FLAG_REBOOT.

#pragma once
#include "bus.h"
#include <stdint.h>

#define BLOB_BUF_SIZE 256  // max single-value payload

// Commit callback: invoked when BLOB_COMMIT arrives for a matching target.
// data/len are the assembled value; flags from the COMMIT frame.
typedef void (*BlobCommitCb)(uint8_t ns, uint8_t key,
                              const uint8_t* data, uint16_t len, uint8_t flags);

void blob_set_commit_cb(BlobCommitCb cb);

// Call from the frame dispatch loop on every received frame.
void blob_handle_frame(const BusFrame& f);

// Send a value as chunked BLOB_WRITE frames + BLOB_COMMIT.
// flags are forwarded in the COMMIT frame for the receiving node to act on.
void blob_send(uint8_t target, uint8_t ns, uint8_t key,
               const uint8_t* data, uint16_t len, uint8_t flags);
