TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all phase0-check phase1-check phase2-check phase3-check phase4-check phase4-hw-test \
        phase5-check phase5-hw-test phase6-check phase6-candidate phase6-hw-test \
        phase7-check phase7-candidate phase7-fault-bootloader phase7-fault-image phase7-hw-test \
        phase8-check phase8-good-candidate phase8-bad-candidate phase8-hw-test \
        phase9-check phase9-candidate phase9-prepare-gateway phase9-gateway-build phase9-hw-test \
        phase10-check phase10-candidate phase10-gateway-build phase10-hw-test \
        bootloader application firmware combined \
        flash-bootloader flash-application flash-combined dump-metadata erase-metadata \
        gateway tools test release clean toolchain-info

all: phase10-check

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


firmware: bootloader application

bootloader:
	$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG)

application:
	$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG)

combined: firmware
	@python3 tools/merge_images.py \
		--output dist/secure-delta-ota-phase10.bin \
		--label "Phase 10"

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

gateway: phase10-gateway-build

tools:
	@python3 -m compileall -q tools server scripts

test: phase10-check

release:
	@echo "Signed release pipeline is Phase 15."

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
	@rm -rf node-stm32f103/bootloader/build-phase7-fault
	@rm -rf node-stm32f103/bootloader/out-phase7-fault
	@rm -rf gateway-esp32/build gateway-esp32/sdkconfig dist build-host
