TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all check benchmark hil-test \
        bootloader application firmware combined \
        gateway gateway-build release \
        flash-bootloader flash-application flash-combined \
        dump-metadata erase-metadata \
        tools test clean toolchain-info

all: check

check:
	@$(TOOLCHAIN_ARG) python3 scripts/project_check.py

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
