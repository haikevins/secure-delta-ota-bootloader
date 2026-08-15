TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all phase0-check phase1-check phase2-check phase3-check phase4-check phase4-hw-test \
        phase5-check phase5-hw-test phase6-check phase6-candidate phase6-hw-test \
        bootloader application firmware combined \
        flash-bootloader flash-application flash-combined dump-metadata erase-metadata \
        gateway tools test release clean toolchain-info

all: phase6-check

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

firmware: bootloader application

bootloader:
	$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG)

application:
	$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG)

combined: firmware
	@python3 tools/merge_images.py --output dist/secure-delta-ota-phase6.bin --label "Phase 6"

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

gateway:
	@echo "ESP32 integration starts in Phase 9."

tools:
	@python3 -m compileall -q tools server scripts

test: phase6-check

release:
	@echo "Signed release pipeline is Phase 15."

clean:
	@$(MAKE) -C node-stm32f103/bootloader clean
	@$(MAKE) -C node-stm32f103/application clean
	@rm -rf node-stm32f103/application/build-phase6-candidate
	@rm -rf node-stm32f103/application/out-phase6-candidate
	@rm -rf gateway-esp32/build dist build-host
