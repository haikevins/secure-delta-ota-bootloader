.PHONY: all phase0-check bootloader application gateway tools test release clean

all: phase0-check

phase0-check:
	@python3 scripts/phase0_check.py

bootloader:
	$(MAKE) -C node-stm32f103/bootloader

application:
	$(MAKE) -C node-stm32f103/application

gateway:
	@echo "Phase 0 skeleton: ESP-IDF implementation starts in Phase 9."

tools:
	@python3 -m compileall -q tools server scripts

test:
	@echo "Phase 0 skeleton: executable tests start in Phase 1."

release:
	@echo "Phase 0 skeleton: release pipeline starts in Phase 15."

clean:
	@rm -rf build out gateway-esp32/build
