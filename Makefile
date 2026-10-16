# Minimal devkitPPC Makefile (recipes use ">" instead of tabs)
.RECIPEPREFIX := >

DEVKITPRO ?= /opt/devkitpro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC

CC      := $(DEVKITPPC)/bin/powerpc-eabi-gcc
ELF2DOL := $(DEVKITPRO)/tools/bin/elf2dol

TARGET  := boot
SOURCES := $(sort $(wildcard source/*.c) source/assets_data.c)
OBJECTS := $(SOURCES:.c=.o)

MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
INCLUDE := -I$(DEVKITPRO)/libogc/include \
           -I$(DEVKITPRO)/portlibs/wii/include \
           -I$(DEVKITPRO)/portlibs/ppc/include \
           -I$(DEVKITPRO)/portlibs/ppc/include/freetype2
LIBDIRS := -L$(DEVKITPRO)/libogc/lib/wii \
           -L$(DEVKITPRO)/portlibs/ppc/lib \
           -L$(DEVKITPRO)/portlibs/wii/lib
LIBS    := -lgrrlib -lpngu -lfreetype -lbrotlidec -lbrotlicommon -lbz2 -lpng -ljpeg -lz -lfat -lwiiuse -lbte -logc -lm

CFLAGS  := -O2 -Wall -std=gnu11 $(MACHDEP) $(INCLUDE)
LDFLAGS := -g $(MACHDEP) -Wl,-Map,$(TARGET).map

ASSET_FILES := $(wildcard assets/*.png assets/*.jpg assets/*.jpeg)

all: $(TARGET).dol

# Textures in assets/ are compiled into the program (source/assets_data.c is generated)
source/assets_data.c: $(ASSET_FILES) tools/embed_assets.py
> python3 tools/embed_assets.py assets source

$(TARGET).elf: $(OBJECTS)
> $(CC) $(LDFLAGS) $(OBJECTS) $(LIBDIRS) $(LIBS) -o $@

$(TARGET).dol: $(TARGET).elf
> $(ELF2DOL) $< $@

%.o: %.c
> $(CC) $(CFLAGS) -c $< -o $@

clean:
> rm -f $(OBJECTS) $(TARGET).elf $(TARGET).dol $(TARGET).map source/assets_data.c

.PHONY: all clean
