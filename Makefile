
PS3DEV := /usr/local/ps3dev
include $(PS3DEV)/ppu_rules

APPID := JEZZ00001
TITLE := JEZZ Music Player
TARGET := JEZZ

CFLAGS := -O2 -Wall
CXXFLAGS := -O2 -Wall

LDFLAGS := -L$(PS3DEV)/ppu/lib
LIBS := -laudio -llv2 -lsysutil

SOURCES := src/main.c
OBJECTS := $(SOURCES:.c=.o)

all: $(TARGET).elf

$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -I$(PS3DEV)/ppu/include -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET).elf $(TARGET).self $(TARGET).pkg

.PHONY: all clean

