# Makefile — configure and compile the unified accessory_node firmware.
#
# Each physical ESP32 gets a dedicated config header in firmware/configs/.
# The Makefile copies the right one to accessory_node/node_config.h before
# invoking arduino-cli, so the single sketch folder compiles to the correct
# firmware for each node.
#
# Usage:
#   make relay                                        # compile
#   make switch
#   make viper
#   make ecu
#   make all                                          # compile all four
#   make upload-relay   PORT=/dev/cu.usbserial-XXXX
#   make upload-switch  PORT=/dev/cu.usbserial-YYYY
#   make upload-viper   PORT=/dev/cu.usbserial-ZZZZ
#   make upload-ecu     PORT=/dev/cu.usbserial-WWWW
#   make monitor        PORT=/dev/cu.usbserial-XXXX

FQBN    := esp32:esp32:esp32
JOBS    := 8
BAUD    := 115200
UPLOAD_SPEED ?= 115200 # 921600
PORT    ?= /dev/cu.SLAB_USBtoUART   # switch panel
# PORT  ?= /dev/cu.usbserial-0001   # relay controller

SKETCH  := firmware/accessory_node
CONFIGS := firmware/configs
MINIFY  := $(SKETCH)/minify_index_html.sh
HTMLDST := $(SKETCH)/index_html.h

# ---- select node config ----
.PHONY: relay switch viper ecu bridge

relay:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/relay_controller.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) $(SKETCH)

switch:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/switch_panel.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) $(SKETCH)

viper:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/viper_interface.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) $(SKETCH)

ecu:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/ecu_node.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) $(SKETCH)

bridge:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/bridge.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) $(SKETCH)

# ---- compile all ----
.PHONY: all
all: relay switch viper ecu bridge

# ---- upload ----
.PHONY: upload-relay upload-switch upload-viper upload-ecu upload-bridge

upload-relay: relay
	@pids=$$(lsof -t $(PORT) 2>/dev/null); [ -n "$$pids" ] && kill -9 $$pids 2>/dev/null || true
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) --upload-field upload.speed=$(UPLOAD_SPEED) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

upload-switch: switch
	@pids=$$(lsof -t $(PORT) 2>/dev/null); [ -n "$$pids" ] && kill -9 $$pids 2>/dev/null || true
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) --upload-field upload.speed=$(UPLOAD_SPEED) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

upload-viper: viper
	@pids=$$(lsof -t $(PORT) 2>/dev/null); [ -n "$$pids" ] && kill -9 $$pids 2>/dev/null || true
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) --upload-field upload.speed=$(UPLOAD_SPEED) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

upload-ecu: ecu
	@pids=$$(lsof -t $(PORT) 2>/dev/null); [ -n "$$pids" ] && kill -9 $$pids 2>/dev/null || true
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) --upload-field upload.speed=$(UPLOAD_SPEED) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

upload-bridge: bridge
	@pids=$$(lsof -t $(PORT) 2>/dev/null); [ -n "$$pids" ] && kill -9 $$pids 2>/dev/null || true
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) --upload-field upload.speed=$(UPLOAD_SPEED) $(SKETCH)
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

# ---- serial monitor ----
.PHONY: monitor
monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)
