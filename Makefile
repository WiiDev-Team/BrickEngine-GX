# Minimal devkitPPC Makefile (recipes use ">" instead of tabs)
.RECIPEPREFIX := >

DEVKITPRO ?= /opt/devkitpro
DEVKITPPC ?= $(DEVKITPRO)/devkitPPC

CC      := $(DEVKITPPC)/bin/powerpc-eabi-gcc
ELF2DOL := $(DEVKITPRO)/tools/bin/elf2dol

TARGET  := boot
SOURCES := $(wildcard source/*.c)
OBJECTS := $(SOURCES:.c=.o)

MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
INCLUDE := -I$(DEVKITPRO)/libogc/include \
           -I$(DEVKITPRO)/portlibs/ppc/include \
           -I$(DEVKITPRO)/portlibs/ppc/include/freetype2 \
           -I$(DEVKITPRO)/portlibs/wii/include 
LIBDIRS := -L$(DEVKITPRO)/libogc/lib/wii \
           -L$(DEVKITPRO)/portlibs/ppc/lib \
           -L$(DEVKITPRO)/portlibs/wii/lib
LIBS    := -lgrrlib -lpngu -lfreetype -lbrotlidec -lbrotlicommon -lbz2 -lpng -ljpeg -lz -lfat -lwiiuse -lbte -logc -lm

CFLAGS  := -O2 -Wall -std=gnu11 $(MACHDEP) $(INCLUDE)
LDFLAGS := -g $(MACHDEP) -Wl,-Map,$(TARGET).map

all: $(TARGET).dol

$(TARGET).elf: $(OBJECTS)
> $(CC) $(LDFLAGS) $(OBJECTS) $(LIBDIRS) $(LIBS) -o $@

$(TARGET).dol: $(TARGET).elf
> $(ELF2DOL) $< $@

%.o: %.c
> $(CC) $(CFLAGS) -c $< -o $@

clean:
> rm -f $(OBJECTS) $(TARGET).elf $(TARGET).dol $(TARGET).map

.PHONY: all clean
