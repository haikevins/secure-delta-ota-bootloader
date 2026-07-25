TOOLCHAIN_ARG := $(if $(strip $(TOOLCHAIN)),TOOLCHAIN=$(TOOLCHAIN),)

.PHONY: all phase0-check phase1-check bootloader application firmware gateway tools test release clean toolchain-info

all: phase1-check

phase0-check:
	@python3 scripts/phase0_check.py

phase1-check: phase0-check
	@$(TOOLCHAIN_ARG) python3 scripts/phase1_check.py

firmware: bootloader application

bootloader:
	$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG)

application:
	$(MAKE) -C node-stm32f103/application $(TOOLCHAIN_ARG)

toolchain-info:
	@$(MAKE) -C node-stm32f103/bootloader $(TOOLCHAIN_ARG) info

gateway:
	@echo "ESP32 build begins in Phase 9; the Phase 1 target covers STM32 firmware only."

tools:
	@python3 -m compileall -q tools server scripts

test:
	@python3 scripts/phase0_check.py
	@$(TOOLCHAIN_ARG) python3 scripts/phase1_check.py

release:
	@echo "The signed release pipeline is implemented in Phase 15."

clean:
	@$(MAKE) -C node-stm32f103/bootloader clean
	@$(MAKE) -C node-stm32f103/application clean
	@rm -rf gateway-esp32/build
