# Makefile — configure and compile the unified accessory_node firmware.
#
# Each physical ESP32 gets a dedicated config header in firmware/configs/.
# The Makefile copies the right one to accessory_node/node_config.h before
# invoking arduino-cli, so the single sketch folder compiles to the correct
# firmware for each node.
#
# Usage:
#   make relay_controller                             # compile
#   make switch_panel
#   make viper_interface
#   make all                                          # compile all three
#   make upload-relay_controller  PORT=/dev/cu.usbserial-XXXX
#   make upload-switch_panel      PORT=/dev/cu.usbserial-YYYY
#   make upload-viper_interface   PORT=/dev/cu.usbserial-ZZZZ
#   make monitor                  PORT=/dev/cu.usbserial-XXXX

FQBN    := esp32:esp32:esp32
BAUD    := 115200
PORT    ?= /dev/cu.usbserial-0001

SKETCH  := firmware/accessory_node
CONFIGS := firmware/configs

# ---- select node config ----
.PHONY: relay_controller switch_panel viper_interface

relay_controller:
	cp $(CONFIGS)/relay_controller.h $(SKETCH)/node_config.h
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

switch_panel:
	cp $(CONFIGS)/switch_panel.h $(SKETCH)/node_config.h
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

viper_interface:
	cp $(CONFIGS)/viper_interface.h $(SKETCH)/node_config.h
	arduino-cli compile --fqbn $(FQBN) $(SKETCH)

# ---- compile all three ----
.PHONY: all
all: relay_controller switch_panel viper_interface

# ---- upload ----
.PHONY: upload-relay_controller upload-switch_panel upload-viper_interface

upload-relay_controller: relay_controller
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

upload-switch_panel: switch_panel
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

upload-viper_interface: viper_interface
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

# ---- serial monitor ----
.PHONY: monitor
monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)
