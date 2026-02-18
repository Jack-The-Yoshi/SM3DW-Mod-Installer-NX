#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Project Settings
#---------------------------------------------------------------------------------

TARGET      := SM3DWModInstallerNX
BUILD       := build
SOURCES     := .
DATA        :=
INCLUDES    :=
ROMFS       :=
ICON        := icon.jpg

APP_TITLE   := SM3DW Mod Installer NX
APP_AUTHOR  := Jack The Yoshi
APP_VERSION := 1.0.0

#---------------------------------------------------------------------------------
# Compiler / Linker Flags
#---------------------------------------------------------------------------------

ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS  := -g -O2 -Wall -ffunction-sections $(ARCH) $(DEFINES) $(INCLUDE) -D__SWITCH__
CFLAGS  += -std=c11
CXXFLAGS:= $(CFLAGS)
ASFLAGS := -g $(ARCH)

LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
           -Wl,-Map,$(notdir $*.map) \
           -Wl,--start-group $(LIBS) -Wl,--end-group

# Libraries
LIBS := -lcurl -llz4 -lzstd -llzma -lbz2 -lz -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# Do not edit below
#---------------------------------------------------------------------------------

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)

export VPATH   := $(CURDIR)
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES    := $(wildcard *.c)
CPPFILES  :=
SFILES    :=
BINFILES  :=

export LD := $(CC)

export OFILES_SRC := $(CFILES:.c=.o)
export OFILES     := $(OFILES_SRC)

export INCLUDE    := $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)

export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

# ---------- Icon Handling ----------
ifeq ($(strip $(NO_ICON)),)
    ifeq ($(wildcard $(ICON)),)
        export NROFLAGS += --icon=$(LIBNX)/default_icon.jpg
    else
        export NROFLAGS += --icon=$(TOPDIR)/$(ICON)
    endif
endif

# ---------- NACP ----------
ifeq ($(strip $(NO_NACP)),)
    export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

.PHONY: all $(BUILD) clean

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
#---------------------------------------------------------------------------------