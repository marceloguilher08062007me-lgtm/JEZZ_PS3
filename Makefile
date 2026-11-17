
.SUFFIXES:

ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT nao esta configurado)
endif

include $(PSL1GHT)/ppu_rules

TARGET := JEZZ
BUILD := build
SOURCES := src
INCLUDES := include

LIBS := -laudio -llv2 -lsysutil

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

export LD := $(CC)
export OFILES := $(CFILES:.c=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(LIBPSL1GHT_INC) \
                  -I$(CURDIR)/$(BUILD) \
                  -I$(PORTLIBS)/include

export CFLAGS := -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
export LDFLAGS := $(MACHDEP)
export LIBPATHS := $(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib
export LIBS

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	rm -rf $(BUILD) $(OUTPUT).elf $(OUTPUT).self

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).elf: $(OFILES)

$(OUTPUT).self: $(OUTPUT).elf

-include $(DEPENDS)

endif
