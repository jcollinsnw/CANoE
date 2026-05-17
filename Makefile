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
JOBS    := 16
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

# ---- preflight checks ----
# These give a one-line "do this to fix it" message instead of letting the
# compiler emit a cryptic "fatal error: secrets.h: No such file" or similar.
.PHONY: check-secrets check-lib-m5cardputer check-lib-pubsub check-lib-nimble

check-secrets:
	@if [ ! -f $(SKETCH)/secrets.h ]; then \
	  echo "!! Missing $(SKETCH)/secrets.h (gitignored — each checkout needs its own)."; \
	  echo "   Fix: cp $(SKETCH)/secrets.h.example $(SKETCH)/secrets.h"; \
	  echo "   Then edit it with your AP_SSID / AP_PASSWORD / ESP-NOW keys."; \
	  exit 1; \
	fi

check-lib-m5cardputer:
	@if ! arduino-cli lib list M5Cardputer 2>/dev/null | grep -q M5Cardputer; then \
	  echo "!! M5Cardputer Arduino library not installed."; \
	  echo "   Fix: arduino-cli lib install M5Cardputer"; \
	  echo "   (pulls in M5Unified, M5GFX, IRremote, LibSSH-ESP32 automatically)"; \
	  exit 1; \
	fi

check-lib-pubsub:
	@if ! arduino-cli lib list PubSubClient 2>/dev/null | grep -q PubSubClient; then \
	  echo "!! PubSubClient Arduino library not installed (required by bridge MQTT)."; \
	  echo "   Fix: arduino-cli lib install \"PubSubClient\""; \
	  echo "   (only needed if MQTT_BROKER is defined in configs/bridge.h)"; \
	  exit 1; \
	fi

check-lib-nimble:
	@if ! arduino-cli lib list NimBLE-Arduino 2>/dev/null | grep -q NimBLE-Arduino; then \
	  echo "!! NimBLE-Arduino library not installed (required by mod_bluetooth)."; \
	  echo "   Fix: arduino-cli lib install \"NimBLE-Arduino\""; \
	  exit 1; \
	fi

# ---- select node config ----
.PHONY: relay switch viper ecu bridge cardputer

relay: check-secrets check-lib-nimble
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/relay_controller.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/relay $(SKETCH)

switch: check-secrets check-lib-nimble
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/switch_panel.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/switch $(SKETCH)

viper: check-secrets
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/viper_interface.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/viper $(SKETCH)

ecu: check-secrets
	@bash $(MINIFY) $(HTMLDST) || true
	cp $(CONFIGS)/ecu_node.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/ecu $(SKETCH)

bridge: check-secrets
	@bash $(MINIFY) $(HTMLDST) || true
	@if grep -q '^[[:space:]]*#define[[:space:]]\+MQTT_BROKER' $(CONFIGS)/bridge.h; then \
	  $(MAKE) check-lib-pubsub; \
	fi
	cp $(CONFIGS)/bridge.h $(SKETCH)/node_config.h
	arduino-cli compile --clean --jobs $(JOBS) --fqbn $(FQBN) --output-dir $(BUILD_DIR)/bridge $(SKETCH)

CARDPUTER_BUILD := $(SKETCH)/build/m5stack.esp32.m5stack_cardputer

cardputer: check-secrets check-lib-m5cardputer
	cp $(CONFIGS)/cardputer.h $(SKETCH)/node_config.h
	arduino-cli compile --jobs $(JOBS) --fqbn $(FQBN_S3) --export-binaries $(SKETCH)
	mkdir -p $(BUILD_DIR)/cardputer
	cp $(CARDPUTER_BUILD)/$(SKETCH_NAME).ino.bin $(BUILD_DIR)/cardputer/m5canoe.bin

# ---- compile all ----
.PHONY: all
all: relay switch viper ecu bridge cardputer

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
# Link-quality preflight: 5 quick pings, abort if any packet is lost or avg RTT
# exceeds OTA_MAX_RTT_MS. OTA over a marginal WiFi link partially writes the OTA
# partition, can wedge macOS WiFi supplicant, and has bricked nodes before.
# Override the gate with OTA_FORCE=1 if you really know what you're doing.
.PHONY: ota-relay ota-switch ota-viper ota-ecu ota-bridge ota-check

OTA_MIN_PINGS   ?= 5
OTA_MAX_RTT_MS  ?= 30
OTA_FORCE       ?= 0

ota-check:
	@if [ "$(OTA_FORCE)" = "1" ]; then \
	  echo ">>> OTA_FORCE=1, skipping link-quality preflight"; \
	else \
	  echo ">>> Link-quality preflight: pinging $(OTA_IP) x$(OTA_MIN_PINGS) ..."; \
	  out=$$(ping -c $(OTA_MIN_PINGS) -W 1000 -i 0.2 $(OTA_IP) 2>&1); \
	  echo "$$out" | tail -2; \
	  loss=$$(echo "$$out" | awk -F',' '/packet loss/ {gsub("%","",$$3); gsub(" ","",$$3); print $$3+0}'); \
	  rtt=$$(echo "$$out"  | awk -F'/' '/min\/avg\/max/ {print int($$5+0.5)}'); \
	  if [ -z "$$rtt" ]; then \
	    echo "!! Preflight failed: no ping reply from $(OTA_IP)."; \
	    echo "   Connect to the node AP, or set OTA_IP=<addr>."; \
	    echo "   Bypass with: make <target> OTA_FORCE=1"; exit 1; \
	  fi; \
	  if [ "$$loss" != "0" ]; then \
	    echo "!! Preflight failed: $${loss}% packet loss to $(OTA_IP)."; \
	    echo "   Move closer to the node and retry."; \
	    echo "   Bypass with: make <target> OTA_FORCE=1"; exit 1; \
	  fi; \
	  if [ "$$rtt" -gt "$(OTA_MAX_RTT_MS)" ]; then \
	    echo "!! Preflight failed: avg RTT $${rtt} ms exceeds OTA_MAX_RTT_MS=$(OTA_MAX_RTT_MS) ms."; \
	    echo "   Link is too slow for reliable OTA; move closer and retry."; \
	    echo "   Bypass with: make <target> OTA_FORCE=1"; exit 1; \
	  fi; \
	  echo ">>> Preflight OK ($${loss}% loss, avg $${rtt} ms RTT)"; \
	fi

# Per-target OTA label + bin path. Recipes share an identical shell snippet.
ota-relay:  OTA_LABEL := relay_controller
ota-relay:  OTA_BIN   := $(BUILD_DIR)/relay/$(SKETCH_NAME).ino.bin
ota-switch: OTA_LABEL := switch_panel
ota-switch: OTA_BIN   := $(BUILD_DIR)/switch/$(SKETCH_NAME).ino.bin
ota-viper:  OTA_LABEL := viper_interface
ota-viper:  OTA_BIN   := $(BUILD_DIR)/viper/$(SKETCH_NAME).ino.bin
ota-ecu:    OTA_LABEL := ecu_node
ota-ecu:    OTA_BIN   := $(BUILD_DIR)/ecu/$(SKETCH_NAME).ino.bin
ota-bridge: OTA_LABEL := bridge
ota-bridge: OTA_BIN   := $(BUILD_DIR)/bridge/$(SKETCH_NAME).ino.bin

ota-relay:  relay  ota-check ota-push
ota-switch: switch ota-check ota-push
ota-viper:  viper  ota-check ota-push
ota-ecu:    ecu    ota-check ota-push
ota-bridge: bridge ota-check ota-push

.PHONY: ota-push
ota-push:
	@echo ">>> Press enter to OTA flash: $(OTA_LABEL) → http://$(OTA_IP)/api/ota"
	@read _
	curl --max-time 120 --connect-timeout 5 -f --progress-bar \
	     -F "firmware=@$(OTA_BIN)" \
	     http://$(OTA_IP)/api/ota
	@echo "Done — node is rebooting"

# ---- flash erase (wipe NVS + app + everything) ----
# A plain `make upload-*` only rewrites the app partition — NVS survives, so
# bad WiFi creds, stale node IDs, and saved rules persist across reflashes.
# Use this when you need a clean slate. Always re-upload firmware afterwards
# (the chip will boot into the ROM bootloader otherwise).
ESPTOOL ?= $(HOME)/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool

.PHONY: erase-flash
erase-flash:
	@if [ ! -x "$(ESPTOOL)" ]; then \
	  echo "!! esptool not found at $(ESPTOOL)"; \
	  echo "   Override with: make erase-flash ESPTOOL=/path/to/esptool PORT=..."; \
	  exit 1; \
	fi
	@echo ">>> Erasing entire flash on $(PORT) (NVS will be wiped)"
	$(ESPTOOL) --chip esp32 --port $(PORT) erase_flash
	@echo ">>> Done. Now run: make upload-<node> PORT=$(PORT)"

# ---- serial monitor ----
.PHONY: monitor screen
monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

# screen gives character-at-a-time input so the shell prompt, echo, and
# backspace all work correctly. Exit with Ctrl-A then Ctrl-\ (or Ctrl-A k).
screen:
	screen $(PORT) $(BAUD)
