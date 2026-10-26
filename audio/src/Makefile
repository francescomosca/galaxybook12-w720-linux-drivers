CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
TARGET = alc298_init
PREFIX ?= /usr/local

all: $(TARGET)

$(TARGET): alc298_init.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)
	install -D -m 644 alc298-init.service $(DESTDIR)/etc/systemd/system/alc298-init.service
	install -D -m 644 alc298-resume.service $(DESTDIR)/etc/systemd/system/alc298-resume.service

.PHONY: all clean install
