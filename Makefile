ifneq ($(V), 1)
MFLAGS += --no-print-dir
Q := @
endif

all:
	$(Q)if [ ! -f libopencm3/Makefile ]; then \
		echo "Initialising git submodules..." ;\
		git submodule init ;\
		git submodule update ;\
	fi
	$(Q)$(MAKE) $(MFLAGS) -C libopencm3 TARGETS=stm32/f1
	$(Q)$(MAKE) $(MFLAGS) -C opendps
	$(Q)$(MAKE) $(MFLAGS) -C dpsboot

clean:
	$(Q)$(MAKE) $(MFLAGS) -C libopencm3 $@
	$(Q)$(MAKE) $(MFLAGS) -C opendps $@
	$(Q)$(MAKE) $(MFLAGS) -C dpsboot $@

flash:
	$(Q)$(MAKE) $(MFLAGS) -C opendps $@
	$(Q)$(MAKE) $(MFLAGS) -C dpsboot $@
