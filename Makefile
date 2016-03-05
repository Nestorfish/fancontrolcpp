CC      = g++
CFLAGS  = -std=gnu++11 -g -Wall -Wextra -pedantic -O2
LDFLAGS = -lboost_program_options
SBIN     = $(DESTDIR)/usr/sbin

all: fancontrolcpp calibrate-fancontrolcpp

calibrate-fancontrolcpp: calibrate.o fancontroller.o
	$(CC) -o $@ $^ $(LDFLAGS)
	objcopy --only-keep-debug $@ $@-dbg
	strip --strip-debug --strip-unneeded $@
	objcopy --add-gnu-debuglink=$@-dbg $@

fancontrolcpp: fancontrol.o fancontroller.o
	$(CC) -o $@ $^ $(LDFLAGS)
	objcopy --only-keep-debug $@ $@-dbg
	strip --strip-debug --strip-unneeded $@
	objcopy --add-gnu-debuglink=$@-dbg $@

calibrate.o: calibrate.cpp lib/fancontroller.h
	$(CC) -c $(CFLAGS) $<

fancontrol.o: fancontrol.cpp lib/fancontroller.h
	$(CC) -c $(CFLAGS) $<

fancontroller.o: fancontroller.cpp lib/fancontroller.h
	$(CC) -c $(CFLAGS) $<

install: all
	install -d $(SBIN)
	install ./fancontrolcpp $(SBIN)
	install ./calibrate-fancontrolcpp $(SBIN)

uninstall:
	rm $(SBIN)/fancontrolcpp $(SBIN)/calibrate-fancontrolcpp

.PHONY: clean cleanest

clean:
	rm -f *.o

distclean: clean
	rm -f fancontrolcpp fancontrolcpp-dbg calibrate-fancontrolcpp calibrate-fancontrolcpp-dbg
