TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all phase0-check phase1-check phase2-check bootloader application firmware combined \
        flash-bootloader flash-application flash-combined gateway tools test release clean toolchain-info

all: phase2-check

phase0-check:
	@python3 scripts/phase0_check.py

phase1-check: phase0-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase1_check.py

phase2-check: phase0-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase2_check.py

firmware: bootloader application

bootloader:
	$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG)

application:
	$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG)

combined: firmware
	@python3 tools/merge_phase2_images.py

flash-bootloader:
	@scripts/flash_bootloader.sh

flash-application:
	@scripts/flash_application.sh

flash-combined:
	@scripts/flash_combined.sh

toolchain-info:
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) info

gateway:
	@echo "ESP32 build begins in Phase 9; the Phase 2 target covers STM32 firmware only."

tools:
	@python3 -m compileall -q tools server scripts

test: phase2-check

release:
	@echo "The signed release pipeline is implemented in Phase 15."

clean:
	@$(MAKE) -C node-stm32f103/bootloader clean
	@$(MAKE) -C node-stm32f103/application clean
	@rm -rf gateway-esp32/build dist
