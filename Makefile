export LC_ALL = C
CXXFLAGS = -std=gnu++14 -g -Wall -Wextra -pedantic -O2
LDFLAGS = -Wl,--as-needed
LDLIBS = -lboost_program_options
LINK.o = $(LINK.cc)
SBIN = $(DESTDIR)/usr/sbin

all: fancontrolcpp calibrate-fancontrolcpp

debug: CXXFLAGS += -DDEBUG -DMY_DEBUG
debug: all

calibrate-fancontrolcpp: calibrate.o fancontroller.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@ && \
		objcopy --only-keep-debug $@ $@-dbg && \
		strip --strip-debug --strip-unneeded $@ && \
		objcopy --add-gnu-debuglink=$@-dbg $@

fancontrolcpp: fancontrol.o fancontroller.o
	$(LINK.o) $^ $(LOADLIBES) $(LDLIBS) -o $@ && \
		objcopy --only-keep-debug $@ $@-dbg && \
		strip --strip-debug --strip-unneeded $@ && \
		objcopy --add-gnu-debuglink=$@-dbg $@

calibrate.o: calibrate.cpp lib/fancontroller.h

fancontrol.o: fancontrol.cpp lib/fancontroller.h

fancontroller.o: fancontroller.cpp lib/fancontroller.h


.PHONY: install uninstall clean cleanest

install: all
	install -d $(SBIN)
	install ./fancontrolcpp $(SBIN)
	install ./calibrate-fancontrolcpp $(SBIN)

uninstall:
	rm -f $(SBIN)/fancontrolcpp $(SBIN)/calibrate-fancontrolcpp

clean:
	rm -f *.o

cleanest: clean
	rm -f fancontrolcpp fancontrolcpp-dbg calibrate-fancontrolcpp calibrate-fancontrolcpp-dbg
