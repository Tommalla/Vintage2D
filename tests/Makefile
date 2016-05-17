TESTS = \
		test-simple \
		test-invalid \
		test-mmap-later \
		test-large-canvas \
		test-long-queue \
		test-multi-context \
		test-big-writes \
		test-multi-device

CFLAGS = -m32 -g
LDFLAGS = -m32

test: $(TESTS:%=%.test)
all: $(TESTS)

SHELL = /bin/bash
.PHONY: %.test
%.test: % %-output /dev/v2d0
	-diff -q <(./$<-output) <(./$<)

test-multi-device.test: /dev/v2d1

.SECONDARY: $(TESTS)
test%: test%.o v2d-testlib.o
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(TESTS) *.o

install:
	cp -au ./* $(HOME)/qemu/hshare/
