TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all phase0-check phase1-check phase2-check phase3-check phase4-check phase4-hw-test \
        phase5-check phase5-hw-test phase6-check phase6-candidate phase6-hw-test \
        phase7-check phase7-candidate phase7-fault-bootloader phase7-fault-image phase7-hw-test \
        phase8-check phase8-good-candidate phase8-bad-candidate phase8-hw-test \
        phase9-check phase9-candidate phase9-prepare-gateway phase9-gateway-build phase9-hw-test \
        phase10-check phase10-candidate phase10-gateway-build phase10-hw-test \
        phase11-check phase11-candidate phase11-gateway-build phase11-hw-test \
        phase12-check phase12-base phase12-target phase12-delta \
        phase13-check phase13-base phase13-target phase13-delta phase13-baseline phase13-hw-test \
        phase14-check phase14-v1 phase14-v2 phase14-v3 phase14-hw-test \
        phase15-check phase15-hw-test phase15-gateway-build phase15-release \
        phase16-check phase16-hw-test \
        phase17-check phase17-benchmark \
        bootloader application firmware combined \
        flash-bootloader flash-application flash-combined dump-metadata erase-metadata \
        gateway tools test release clean toolchain-info

all: phase17-check

phase0-check:
	@python3 scripts/phase0_check.py

phase1-check: phase0-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase1_check.py

phase2-check: phase0-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase2_check.py

phase3-check: phase0-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase3_check.py

phase4-check: phase3-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase4_check.py

phase4-hw-test:
	@$(TOOLCHAIN_ARG) python3 scripts/phase4_hw_test.py

phase5-check: phase4-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase5_check.py

phase5-hw-test:
	@PORT="$(PORT)" python3 scripts/phase5_hw_test.py

phase6-check: phase5-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase6_check.py

phase6-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase6-candidate OUT_DIR=out-phase6-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase6-candidate OUT_DIR=out-phase6-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase6-hw-test: phase6-candidate
	@PORT="$(PORT)" python3 scripts/phase6_hw_test.py

phase7-check: phase6-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase7_check.py

phase7-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase7-candidate OUT_DIR=out-phase7-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase7-candidate OUT_DIR=out-phase7-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase7-fault-bootloader:
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase7-fault OUT_DIR=out-phase7-fault \
		PROJECT_CFLAGS="-DPHASE7_FAULT_INJECT_OFFSET=1536UL" clean
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase7-fault OUT_DIR=out-phase7-fault \
		PROJECT_CFLAGS="-DPHASE7_FAULT_INJECT_OFFSET=1536UL" all

phase7-fault-image: bootloader application phase7-candidate phase7-fault-bootloader
	@python3 tools/merge_images.py \
		--bootloader node-stm32f103/bootloader/out-phase7-fault/bootloader.bin \
		--application node-stm32f103/application/out/application.bin \
		--output dist/secure-delta-ota-phase7-fault.bin \
		--label "Phase 7 fault-injection"

phase7-hw-test: phase7-fault-image
	@PORT="$(PORT)" python3 scripts/phase7_hw_test.py

phase8-check: phase7-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase8_check.py

phase8-good-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase8-good OUT_DIR=out-phase8-good \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase8-good OUT_DIR=out-phase8-good \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase8-bad-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase8-bad OUT_DIR=out-phase8-bad \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000003UL -DPHASE8_DISABLE_TRIAL_CONFIRM=1" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase8-bad OUT_DIR=out-phase8-bad \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000003UL -DPHASE8_DISABLE_TRIAL_CONFIRM=1" all

phase8-hw-test: bootloader application phase8-good-candidate phase8-bad-candidate combined
	@PORT="$(PORT)" python3 scripts/phase8_hw_test.py


phase9-check: phase8-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase9_check.py

phase9-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase9-candidate OUT_DIR=out-phase9-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase9-candidate OUT_DIR=out-phase9-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase9-prepare-gateway: phase9-candidate
	@python3 scripts/phase9_prepare_gateway.py

phase9-gateway-build: phase9-prepare-gateway
	@command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found. Source ESP-IDF export.sh first."; exit 1; }
	@python3 scripts/esp32_build_guard.py
	@cd gateway-esp32 && idf.py build

phase9-hw-test:
	@echo "Cleaning relocation-sensitive STM32 dependency/object files..."
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) clean
	@$(MAKE) bootloader application phase9-gateway-build combined
	@ESP32_PORT="$(ESP32_PORT)" PORT="$(PORT)" python3 scripts/phase9_hw_test.py


phase10-check:
	@$(TOOLCHAIN_ARG) python3 scripts/phase10_check.py

phase10-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase10-candidate OUT_DIR=out-phase10-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase10-candidate OUT_DIR=out-phase10-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase10-gateway-build:
	@command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found. Source ESP-IDF export.sh first."; exit 1; }
	@python3 scripts/esp32_build_guard.py
	@cd gateway-esp32 && idf.py build

phase10-hw-test:
	@echo "Cleaning relocation-sensitive STM32 dependency/object files..."
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) clean
	@$(MAKE) bootloader application phase10-candidate combined
	@ESP32_PORT="$(ESP32_PORT)" PORT="$(PORT)" \
		WIFI_SSID="$(WIFI_SSID)" WIFI_PASSWORD="$(WIFI_PASSWORD)" \
		HTTPS_HOST_IP="$(HTTPS_HOST_IP)" HTTPS_PORT="$(HTTPS_PORT)" \
		python3 scripts/phase10_hw_test.py



phase11-check:
	@$(TOOLCHAIN_ARG) python3 scripts/phase11_check.py

phase11-candidate:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase11-candidate OUT_DIR=out-phase11-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase11-candidate OUT_DIR=out-phase11-candidate \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase11-gateway-build:
	@command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found. Source ESP-IDF export.sh first."; exit 1; }
	@python3 scripts/esp32_build_guard.py
	@cd gateway-esp32 && idf.py build

phase11-hw-test:
	@echo "Cleaning relocation-sensitive STM32 dependency/object files..."
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) clean
	@$(MAKE) bootloader application phase11-candidate combined
	@ESP32_PORT="$(ESP32_PORT)" PORT="$(PORT)" \
		WIFI_SSID="$(WIFI_SSID)" WIFI_PASSWORD="$(WIFI_PASSWORD)" \
		PHASE11_HOST_IP="$(PHASE11_HOST_IP)" HTTPS_HOST_IP="$(HTTPS_HOST_IP)" \
		HTTPS_PORT="$(HTTPS_PORT)" MQTT_PORT="$(MQTT_PORT)" \
		python3 scripts/phase11_hw_test.py



phase12-base:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase12-base OUT_DIR=out-phase12-base \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000001UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase12-base OUT_DIR=out-phase12-base \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000001UL" all

phase12-target:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase12-target OUT_DIR=out-phase12-target \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase12-target OUT_DIR=out-phase12-target \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase12-delta: phase12-base phase12-target
	@python3 tools/phase12_delta.py \
		--base node-stm32f103/application/out-phase12-base/application.bin \
		--target node-stm32f103/application/out-phase12-target/application.bin \
		--base-version 1 --target-version 2 \
		--output-dir dist/phase12 \
		--min-savings-percent 20

phase12-check:
	@$(TOOLCHAIN_ARG) python3 scripts/phase12_check.py



phase13-base:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase13-base OUT_DIR=out-phase13-base \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000001UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase13-base OUT_DIR=out-phase13-base \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000001UL" all

phase13-target:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase13-target OUT_DIR=out-phase13-target \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase13-target OUT_DIR=out-phase13-target \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase13-delta: phase13-base phase13-target
	@mkdir -p dist/phase13
	@python3 tools/jojodiff_patch.py generate \
		node-stm32f103/application/out-phase13-base/application.bin \
		node-stm32f103/application/out-phase13-target/application.bin \
		dist/phase13/application-v1-to-v2.jdiff
	@python3 tools/phase13_delta_artifact.py \
		--base node-stm32f103/application/out-phase13-base/application.bin \
		--target node-stm32f103/application/out-phase13-target/application.bin \
		--patch dist/phase13/application-v1-to-v2.jdiff \
		--base-version 1 --target-version 2 \
		--output dist/phase13/application-v1-to-v2.d13

phase13-baseline: bootloader phase13-base
	@python3 tools/merge_images.py \
		--bootloader node-stm32f103/bootloader/out/bootloader.bin \
		--application node-stm32f103/application/out-phase13-base/application.bin \
		--output dist/secure-delta-ota-phase13.bin \
		--label "Phase 13"

phase13-check:
	@$(TOOLCHAIN_ARG) python3 scripts/phase13_check.py

phase13-hw-test: phase13-baseline phase13-delta
	@PORT="$(PORT)" STM32_PORT="$(STM32_PORT)" \
		python3 scripts/phase13_hw_test.py



phase14-v1:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase14-v1 OUT_DIR=out-phase14-v1 \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000001UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase14-v1 OUT_DIR=out-phase14-v1 \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000001UL" all

phase14-v2:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase14-v2 OUT_DIR=out-phase14-v2 \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase14-v2 OUT_DIR=out-phase14-v2 \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000002UL" all

phase14-v3:
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase14-v3 OUT_DIR=out-phase14-v3 \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000003UL" clean
	@$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG) \
		BUILD_DIR=build-phase14-v3 OUT_DIR=out-phase14-v3 \
		PROJECT_CFLAGS="-DAPPLICATION_VERSION=0x00000003UL" all

phase14-check:
	@$(TOOLCHAIN_ARG) python3 scripts/phase14_check.py

phase14-hw-test:
	@PORT="$(PORT)" STM32_PORT="$(STM32_PORT)" \
		PHASE14_PRIVATE_KEY="$(PHASE14_PRIVATE_KEY)" \
		PHASE14_KEY_ID="$(PHASE14_KEY_ID)" \
		$(TOOLCHAIN_ARG) python3 scripts/phase14_hw_test.py


phase15-check: phase14-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase15_check.py

phase15-gateway-build:
	@command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found. Source ESP-IDF export.sh first."; exit 1; }
	@python3 scripts/esp32_build_guard.py
	@cd gateway-esp32 && idf.py build

phase15-hw-test:
	@ESP32_PORT="$(ESP32_PORT)" PORT="$(PORT)" \
		WIFI_SSID="$(WIFI_SSID)" WIFI_PASSWORD="$(WIFI_PASSWORD)" \
		PHASE15_HOST_IP="$(PHASE15_HOST_IP)" \
		HTTPS_PORT="$(HTTPS_PORT)" MQTT_PORT="$(MQTT_PORT)" \
		PHASE15_KEY_ID="$(PHASE15_KEY_ID)" \
		$(TOOLCHAIN_ARG) python3 scripts/phase15_hw_test.py

phase15-release:
	@python3 tools/phase15_release.py \
		--target "$(TARGET)" \
		--target-version "$(TARGET_VERSION)" \
		$(if $(strip $(BASE)),--base "$(BASE)" --base-version "$(BASE_VERSION)",) \
		--key "$(SIGNING_KEY)" \
		--key-id "$(KEY_ID)" \
		--base-url "$(BASE_URL)" \
		--channel "$(if $(strip $(CHANNEL)),$(CHANNEL),stable)" \
		--output-root "$(if $(strip $(RELEASE_ROOT)),$(RELEASE_ROOT),dist/releases)"


phase16-check: phase15-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase16_check.py

phase16-hw-test:
	@ESP32_PORT="$(ESP32_PORT)" PORT="$(PORT)" \
		WIFI_SSID="$(WIFI_SSID)" WIFI_PASSWORD="$(WIFI_PASSWORD)" \
		PHASE16_HOST_IP="$(PHASE16_HOST_IP)" PHASE15_HOST_IP="$(PHASE15_HOST_IP)" \
		HTTPS_PORT="$(HTTPS_PORT)" MQTT_PORT="$(MQTT_PORT)" \
		PHASE16_KEY_ID="$(PHASE16_KEY_ID)" \
		$(TOOLCHAIN_ARG) python3 scripts/phase16_hw_test.py



phase17-check: phase16-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase17_check.py

phase17-benchmark:
	@$(TOOLCHAIN_ARG) python3 scripts/phase17_benchmark.py --output-dir dist/phase17


firmware: bootloader application

bootloader:
	$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG)

application:
	$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG)

combined: firmware
	@python3 tools/merge_images.py \
		--output dist/secure-delta-ota-phase14-unprovisioned.bin \
		--label "Phase 14 (unprovisioned trust anchor)"

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

gateway: phase11-gateway-build

tools:
	@python3 -m compileall -q tools server scripts

test: phase17-check

release:
	@echo "Use make phase15-release TARGET=... TARGET_VERSION=... SIGNING_KEY=/secure/path/key.pem KEY_ID=0x... BASE_URL=https://firmware.example"

clean:
	@$(MAKE) -C node-stm32f103/bootloader clean
	@$(MAKE) -C node-stm32f103/application clean
	@rm -rf node-stm32f103/application/build-phase6-candidate
	@rm -rf node-stm32f103/application/out-phase6-candidate
	@rm -rf node-stm32f103/application/build-phase7-candidate
	@rm -rf node-stm32f103/application/out-phase7-candidate
	@rm -rf node-stm32f103/application/build-phase8-good
	@rm -rf node-stm32f103/application/out-phase8-good
	@rm -rf node-stm32f103/application/build-phase8-bad
	@rm -rf node-stm32f103/application/out-phase8-bad
	@rm -rf node-stm32f103/application/build-phase9-candidate
	@rm -rf node-stm32f103/application/out-phase9-candidate
	@rm -rf node-stm32f103/application/build-phase10-candidate
	@rm -rf node-stm32f103/application/out-phase10-candidate
	@rm -rf node-stm32f103/application/build-phase11-candidate
	@rm -rf node-stm32f103/application/out-phase11-candidate
	@rm -rf node-stm32f103/application/build-phase12-base
	@rm -rf node-stm32f103/application/out-phase12-base
	@rm -rf node-stm32f103/application/build-phase12-target
	@rm -rf node-stm32f103/application/out-phase12-target
	@rm -rf node-stm32f103/application/build-phase13-base
	@rm -rf node-stm32f103/application/out-phase13-base
	@rm -rf node-stm32f103/application/build-phase13-target
	@rm -rf node-stm32f103/application/out-phase13-target
	@rm -rf node-stm32f103/application/build-phase14-v1
	@rm -rf node-stm32f103/application/out-phase14-v1
	@rm -rf node-stm32f103/application/build-phase14-v2
	@rm -rf node-stm32f103/application/out-phase14-v2
	@rm -rf node-stm32f103/application/build-phase14-v3
	@rm -rf node-stm32f103/application/out-phase14-v3
	@rm -rf node-stm32f103/application/build-phase14-hw-v1
	@rm -rf node-stm32f103/application/out-phase14-hw-v1
	@rm -rf node-stm32f103/application/build-phase14-hw-v2
	@rm -rf node-stm32f103/application/out-phase14-hw-v2
	@rm -rf node-stm32f103/application/build-phase14-hw-v3
	@rm -rf node-stm32f103/application/out-phase14-hw-v3
	@rm -rf node-stm32f103/application/build-phase15-base
	@rm -rf node-stm32f103/application/out-phase15-base
	@rm -rf node-stm32f103/application/build-phase15-target
	@rm -rf node-stm32f103/application/out-phase15-target
	@rm -rf node-stm32f103/application/build-phase15-hw-v1
	@rm -rf node-stm32f103/application/out-phase15-hw-v1
	@rm -rf node-stm32f103/application/build-phase15-hw-v2
	@rm -rf node-stm32f103/application/out-phase15-hw-v2
	@rm -rf node-stm32f103/application/build-phase16-*
	@rm -rf node-stm32f103/application/out-phase16-*
	@rm -rf node-stm32f103/bootloader/build-phase16-*
	@rm -rf node-stm32f103/bootloader/out-phase16-*
	@rm -rf node-stm32f103/application/build-phase17-*
	@rm -rf node-stm32f103/application/out-phase17-*
	@rm -rf node-stm32f103/bootloader/build-phase17-*
	@rm -rf node-stm32f103/bootloader/out-phase17-*
	@rm -rf .phase17-benchmark-tmp
	@rm -rf node-stm32f103/bootloader/build-phase7-fault
	@rm -rf node-stm32f103/bootloader/out-phase7-fault
	@rm -rf gateway-esp32/build gateway-esp32/sdkconfig dist build-host
