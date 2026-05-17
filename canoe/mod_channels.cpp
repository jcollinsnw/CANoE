#include "mod_channels.h"

// Populated from CHAN_CAPS_INIT in node_config.h; empty array if not defined.
#ifndef CHAN_CAPS_INIT
#define CHAN_CAPS_INIT {}
#endif

static const CanChanDef g_chan_caps[] = CHAN_CAPS_INIT;
static const uint8_t    g_chan_cap_count =
    sizeof(g_chan_caps) / sizeof(g_chan_caps[0]);

void chan_caps_setup() {
    // Nothing to initialise at runtime — channels are compile-time constants.
}

void chan_caps_send() {
    for (uint8_t i = 0; i < g_chan_cap_count; i++) {
        const CanChanDef& d = g_chan_caps[i];
        uint8_t pkt[8] = {
            bus_node_id(),
            d.chan_id,
            (uint8_t)(d.src_can_id & 0xFF),
            (uint8_t)(d.src_can_id >> 8),
            d.byte_offset,
            d.encoding,
            (uint8_t)d.int_offset,
            d.valid_spec
        };
        bus_tx(CAN_ID_CHAN_CAP, pkt, 8);
    }
}
