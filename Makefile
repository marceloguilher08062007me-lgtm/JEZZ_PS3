.SUFFIXES:

TITLE := JEZZ
APPID := JEZZ00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
ICON0 := $(CURDIR)/ICON0.PNG

ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment")
endif

include $(PSL1GHT)/ppu_rules

TARGET := JEZZ
BUILD := build
SOURCES := src
DATA :=
PKGFILES := $(CURDIR)/pkgfiles
INCLUDES := include
LIBDIRS := $(PORTLIBS)

# libcurl + PolarSSL habilitam comunicacao HTTPS no PS3.
LIBS := -lfont3d -lfreetype -lpng -lpngdec -ltiny3d -lrsx -lz -lsimdmath -lgcm_sys -lio -lcurl -lpolarssl -lnet -lsysutil -lsysmodule -lrt -llv2 -lm

CFLAGS = -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) -I$(PORTLIBS)/include/freetype2
CXXFLAGS = $(CFLAGS)
LDFLAGS = $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
                 $(sFILES:.s=.o) $(SFILES:.S=.o)

export INCLUDE := -I$(PORTLIBS)/include/freetype2 \
                  $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  $(LIBPSL1GHT_INC) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) $(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@rm -rf $(BUILD) JEZZ.elf JEZZ.self JEZZ.pkg JEZZ.elf.map

pkg: $(BUILD) $(OUTPUT).pkg

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
