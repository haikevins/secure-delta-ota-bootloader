TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all help check warning-check benchmark hil-test \
        bootloader application firmware combined \
        gateway gateway-build release \
        flash-bootloader flash-application flash-combined \
        dump-metadata erase-metadata \
        tools test clean toolchain-info

all: check

help:
	@printf '%s\n' \
	  'Secure Delta OTA Bootloader - make targets' \
	  '' \
	  'Validation and quality:' \
	  '  make check [TOOLCHAIN=gcc|clang]          Full integrated host/build/security/benchmark gate' \
	  '  make warning-check [TOOLCHAIN=gcc|clang]  Rebuild STM32 firmware with compiler warnings as errors' \
	  '  make benchmark [TOOLCHAIN=gcc|clang]      Generate JSON/CSV/Markdown benchmark reports' \
	  '  make test [TOOLCHAIN=gcc|clang]           Alias of make check' \
	  '  make tools                                Python syntax/bytecode smoke check for tools/server/scripts' \
	  '' \
	  'STM32 firmware:' \
	  '  make firmware [TOOLCHAIN=gcc|clang]       Build bootloader + application' \
	  '  make bootloader [TOOLCHAIN=gcc|clang]     Build bootloader only' \
	  '  make application [TOOLCHAIN=gcc|clang]    Build application only' \
	  '  make combined [TOOLCHAIN=gcc|clang]       Build and merge bootloader + application binary' \
	  '  make toolchain-info [TOOLCHAIN=...]       Show selected STM32 compiler/linker/output paths' \
	  '' \
	  'ESP32 gateway:' \
	  '  make gateway                              Build gateway with active ESP-IDF environment' \
	  '  make gateway-build                        Same gateway build target, explicit name' \
	  '' \
	  'Release:' \
	  '  make release TARGET=... TARGET_VERSION=N SIGNING_KEY=... KEY_ID=... BASE_URL=https://...' \
	  '                                            Create immutable signed full/delta release artifacts' \
	  '' \
	  'Hardware / OpenOCD:' \
	  '  make flash-bootloader [TOOLCHAIN=...]     Build and flash bootloader with ST-Link' \
	  '  make flash-application [TOOLCHAIN=...]    Build and flash application with ST-Link' \
	  '  make flash-combined [TOOLCHAIN=...]       Build and flash merged image at 0x08000000' \
	  '  make dump-metadata                        Dump and decode internal metadata A/B pages' \
	  '  make erase-metadata                       Erase internal metadata A/B pages (destructive)' \
	  '  make hil-test ...                         Run 9-scenario deterministic hardware fault matrix' \
	  '' \
	  'Maintenance:' \
	  '  make clean                                Remove STM32/ESP32/generated benchmark build outputs' \
	  '' \
	  'See README.md and docs/make-command-reference.md for variables, prerequisites, examples and outputs.'

check:
	@$(TOOLCHAIN_ARG) python3 scripts/project_check.py

warning-check:
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-warning OUT_DIR=out-warning \
		PROJECT_CFLAGS="-Werror" clean all
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-warning OUT_DIR=out-warning \
		PROJECT_CFLAGS="-Werror" clean all
	@rm -rf node-stm32f103/bootloader/build-warning node-stm32f103/bootloader/out-warning
	@rm -rf node-stm32f103/application/build-warning node-stm32f103/application/out-warning
	@echo "WARNING_CLEAN_BUILD=PASS"

benchmark:
	@$(TOOLCHAIN_ARG) python3 scripts/benchmark.py --output-dir dist/benchmark

hil-test:
	@ESP32_PORT="$(ESP32_PORT)" PORT="$(PORT)" \
		WIFI_SSID="$(WIFI_SSID)" WIFI_PASSWORD="$(WIFI_PASSWORD)" \
		SDOTA_HOST_IP="$(SDOTA_HOST_IP)" \
		HTTPS_PORT="$(HTTPS_PORT)" MQTT_PORT="$(MQTT_PORT)" \
		SDOTA_HIL_KEY_ID="$(SDOTA_HIL_KEY_ID)" \
		$(TOOLCHAIN_ARG) python3 scripts/hil_test.py

firmware: bootloader application

bootloader:
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG)

application:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG)

combined: firmware
	@python3 tools/merge_images.py \
		--output dist/secure-delta-ota-combined.bin \
		--label "unprovisioned trust anchor"

gateway: gateway-build

gateway-build:
	@command -v idf.py >/dev/null 2>&1 || { \
		echo "idf.py not found. Source ESP-IDF export.sh first."; exit 1; }
	@python3 scripts/esp32_build_guard.py
	@cd gateway-esp32 && idf.py build

release:
	@python3 tools/release.py \
		--target "$(TARGET)" \
		--target-version "$(TARGET_VERSION)" \
		$(if $(strip $(BASE)),--base "$(BASE)" --base-version "$(BASE_VERSION)",) \
		--key "$(SIGNING_KEY)" \
		--key-id "$(KEY_ID)" \
		--base-url "$(BASE_URL)" \
		--channel "$(if $(strip $(CHANNEL)),$(CHANNEL),stable)" \
		--output-root "$(if $(strip $(RELEASE_ROOT)),$(RELEASE_ROOT),dist/releases)"

flash-bootloader:
	@bash scripts/flash_bootloader.sh

flash-application:
	@bash scripts/flash_application.sh

flash-combined:
	@bash scripts/flash_combined.sh

dump-metadata:
	@bash scripts/dump_metadata.sh

erase-metadata:
	@bash scripts/erase_metadata.sh

toolchain-info:
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) info

tools:
	@python3 -m compileall -q tools server scripts

test: check

clean:
	@$(MAKE) -C node-stm32f103/bootloader clean
	@$(MAKE) -C node-stm32f103/application clean
	@rm -rf node-stm32f103/bootloader/build-* node-stm32f103/bootloader/out-*
	@rm -rf node-stm32f103/application/build-* node-stm32f103/application/out-*
	@rm -rf gateway-esp32/build gateway-esp32/sdkconfig gateway-esp32/sdkconfig.old
	@rm -rf .benchmark-tmp dist build-host
