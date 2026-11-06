# Makefile for Samsung Galaxy Book 12 Auto-Brightness Daemon
CC ?= gcc
PKG_CFLAGS := $(shell pkg-config --cflags gio-2.0 gio-unix-2.0 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs gio-2.0 gio-unix-2.0 2>/dev/null) -lm

SRCS := main.c curve.c config.c brightness.c sensor.c
TARGET := galaxybook-autobrightness

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) -O2 -Wall -Wextra $(SRCS) $(PKG_CFLAGS) $(PKG_LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean

