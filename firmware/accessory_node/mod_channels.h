#pragma once
#include "can_protocol.h"
#include "bus.h"

// Channel capability module — responds to CHAN_CAP_REQ with one CHAN_CAP frame
// per channel this node publishes. Define CHAN_CAPS_INIT in the node config to
// populate the channel list; nodes without it compile to empty stubs.
//
// Typical usage in node config:
//   #define CHAN_CAPS_INIT { \
//       CHAN_DEF(CHAN_ID_RPM, CAN_ID_ENGINE_DATA, 0, CHAN_ENC_U16LE|CHAN_SCALE_1, 0, 0), \
//   }

void chan_caps_setup();

// Send all CHAN_CAP frames for this node. target is ignored (CAN is broadcast);
// kept for symmetry with send_node_cap(). Call at boot and periodically.
void chan_caps_send();
