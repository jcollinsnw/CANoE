# Makefile — sync shared files, then compile/upload ESP32 nodes.
#
# Usage:
#   make sync                        # copy shared/ → each sketch folder
#   make switch_panel                 # sync + compile switch_panel
#   make relay_controller             # sync + compile relay_controller
#   make viper_interface              # sync + compile viper_interface
#   make all                          # sync + compile all three
#   make upload-switch_panel PORT=/dev/cu.usbserial-XXXX
#   make upload-relay_controller PORT=/dev/cu.usbserial-YYYY
#   make upload-viper_interface PORT=/dev/cu.usbserial-ZZZZ
#   make monitor PORT=/dev/cu.usbserial-XXXX

FQBN     := esp32:esp32:esp32
BAUD     := 115200
PORT     ?= /dev/cu.usbserial-0001

SHARED   := firmware/shared
SKETCHES := firmware/relay_controller firmware/switch_panel firmware/viper_interface
SHARED_FILES := can_protocol.h bus.h bus.cpp webui.h webui.cpp index_html.h

# ---- sync ----
.PHONY: sync
sync:
	@for dir in $(SKETCHES); do \
		for f in $(SHARED_FILES); do \
			cp $(SHARED)/$$f $$dir/$$f; \
		done; \
	done
	@echo "[sync] shared files copied to all sketch folders"

# ---- compile individual nodes ----
.PHONY: switch_panel relay_controller viper_interface
switch_panel: sync
	arduino-cli compile --fqbn $(FQBN) firmware/switch_panel

relay_controller: sync
	arduino-cli compile --fqbn $(FQBN) firmware/relay_controller

viper_interface: sync
	arduino-cli compile --fqbn $(FQBN) firmware/viper_interface

# ---- compile all ----
.PHONY: all
all: switch_panel relay_controller viper_interface

# ---- upload ----
.PHONY: upload-switch_panel upload-relay_controller upload-viper_interface
upload-switch_panel: switch_panel
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) firmware/switch_panel

upload-relay_controller: relay_controller
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) firmware/relay_controller

upload-viper_interface: viper_interface
	arduino-cli upload -p $(PORT) --fqbn $(FQBN) firmware/viper_interface

# ---- serial monitor ----
.PHONY: monitor
monitor:
	arduino-cli monitor -p $(PORT) -c baudrate=$(BAUD)

# ---- check shared file consistency ----
.PHONY: check
check:
	@ok=true; \
	for f in $(SHARED_FILES); do \
		for dir in $(SKETCHES); do \
			if ! diff -q $(SHARED)/$$f $$dir/$$f >/dev/null 2>&1; then \
				echo "MISMATCH: $(SHARED)/$$f vs $$dir/$$f"; ok=false; \
			fi; \
		done; \
	done; \
	$$ok && echo "[check] all shared files in sync"
