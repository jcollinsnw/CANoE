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
#   make upload-relay   PORT=/dev/cu.usbserial-XXXX  # USB flash
#   make upload-switch  PORT=/dev/cu.usbserial-YYYY
#   make upload-viper   PORT=/dev/cu.usbserial-ZZZZ
#   make upload-ecu     PORT=/dev/cu.usbserial-WWWW
#   make monitor        PORT=/dev/cu.usbserial-XXXX
#   make ota-relay                                    # WiFi OTA (connect to node AP first)
#   make ota-switch     OTA_IP=192.168.4.1            # OTA_IP defaults to 192.168.4.1

FQBN    := esp32:esp32:esp32:PartitionScheme=min_spiffs
FQBN_S3 := m5stack:esp32:m5stack_cardputer
JOBS    := 8
BAUD    := 115200
UPLOAD_SPEED ?= 115200 # 921600
PORT    ?= /dev/cu.SLAB_USBtoUART   # switch panel
# PORT  ?= /dev/cu.usbserial-0001   # relay controller

SKETCH      := firmware/accessory_node
CONFIGS     := firmware/configs
MINIFY      := $(SKETCH)/minify_index_html.sh
HTMLDST     := $(SKETCH)/index_html.h
BUILD_DIR   := build
SKETCH_NAME := accessory_node
OTA_IP      ?= 192.168.4.1

# Cardputer UF2 / mass-storage deploy (plug in via USB while holding G0 for UF2 mode)
CARDPUTER_VOLUME ?= /Volumes/CARDPUTER
CARDPUTER_DIR    ?= CANoE

# ---- select node config ----
.PHONY: relay switch viper ecu bridge cardputer

relay:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/relay_controller.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/relay $(SKETCH)

switch:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/switch_panel.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/switch $(SKETCH)

viper:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/viper_interface.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/viper $(SKETCH)

ecu:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/ecu_node.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/ecu $(SKETCH)

bridge:
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/bridge.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/bridge $(SKETCH)

CARDPUTER_BUILD := $(SKETCH)/build/m5stack.esp32.m5stack_cardputer

cardputer:
	cp $(CONFIGS)/cardputer.h $(SKETCH)/node_config.h
	arduino-cli compile --jobs $(JOBS) --fqbn $(FQBN_S3) --export-binaries $(SKETCH)
	mkdir -p $(BUILD_DIR)/cardputer
	cp $(CARDPUTER_BUILD)/$(SKETCH_NAME).ino.bin $(BUILD_DIR)/cardputer/m5canoe.bin

# ---- compile all ----
.PHONY: all
all: relay switch viper ecu bridge

# ---- upload ----
.PHONY: upload-relay upload-switch upload-viper upload-ecu upload-bridge upload-cardputer

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

upload-cardputer: cardputer
	@echo ">>> Copying to $(CARDPUTER_VOLUME)/$(CARDPUTER_DIR)/m5canoe.bin ..."
	@test -d "$(CARDPUTER_VOLUME)" || (echo "ERROR: $(CARDPUTER_VOLUME) not mounted — plug in Cardputer" && exit 1)
	mkdir -p "$(CARDPUTER_VOLUME)/$(CARDPUTER_DIR)"
	cp $(BUILD_DIR)/cardputer/m5canoe.bin "$(CARDPUTER_VOLUME)/$(CARDPUTER_DIR)/m5canoe.bin"
	@echo ">>> Unmounting $(CARDPUTER_VOLUME) ..."
	diskutil unmount "$(CARDPUTER_VOLUME)"
	@echo "Done — Cardputer will reboot and load the new firmware"

# ---- OTA upload (connect to node AP first; OTA_IP defaults to 192.168.4.1) ----
.PHONY: ota-relay ota-switch ota-viper ota-ecu ota-bridge

ota-relay: relay
	@echo ">>> Press enter to OTA flash: relay_controller → http://$(OTA_IP)/api/ota"
	read
	curl --max-time 60 -f \
	     -F "firmware=@$(BUILD_DIR)/relay/$(SKETCH_NAME).ino.bin" \
	     http://$(OTA_IP)/api/ota
	@echo "Done — node is rebooting"

ota-switch: switch
	@echo ">>> Press enter to OTA flash: switch_panel → http://$(OTA_IP)/api/ota"
	read
	curl --max-time 60 -f \
	     -F "firmware=@$(BUILD_DIR)/switch/$(SKETCH_NAME).ino.bin" \
	     http://$(OTA_IP)/api/ota
	@echo "Done — node is rebooting"

ota-viper: viper
	@echo ">>> Press enter to OTA flash: viper_interface → http://$(OTA_IP)/api/ota"
	read
	curl --max-time 60 -f \
	     -F "firmware=@$(BUILD_DIR)/viper/$(SKETCH_NAME).ino.bin" \
	     http://$(OTA_IP)/api/ota
	@echo "Done — node is rebooting"

ota-ecu: ecu
	@echo ">>> Press enter to OTA flash: ecu_node → http://$(OTA_IP)/api/ota"
	read
	curl --max-time 60 -f \
	     -F "firmware=@$(BUILD_DIR)/ecu/$(SKETCH_NAME).ino.bin" \
	     http://$(OTA_IP)/api/ota
	@echo "Done — node is rebooting"

ota-bridge: bridge
	@echo ">>> Press enter to OTA flash: bridge → http://$(OTA_IP)/api/ota"
	read
	curl --max-time 60 -f \
	     -F "firmware=@$(BUILD_DIR)/bridge/$(SKETCH_NAME).ino.bin" \
	     http://$(OTA_IP)/api/ota
	@echo "Done — node is rebooting"

# ---- serial monitor ----
.PHONY: monitor screen
monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

# screen gives character-at-a-time input so the shell prompt, echo, and
# backspace all work correctly. Exit with Ctrl-A then Ctrl-\ (or Ctrl-A k).
screen:
	screen $(PORT) $(BAUD)
