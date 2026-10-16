# BrickEngine-GX

A game creation system for the Nintendo Wii: a small grid-based 3D engine built on [GRRLIB](https://github.com/GRRLIB/GRRLIB) / GX, a level editor, and the tools to ship a game as a channel.

Levels are plain text files made of textured columns on a grid (up to 64 × 64). You draw them in a graphical editor, the engine loads them from the SD card, and the textures are compiled straight into the program.

## Features

**Engine**
- Textured 3D grid levels up to **64 × 64** cells, with columns of different heights, a tiled floor and a per-level sky colour
- First-person movement with **Wiimote + Nunchuk**, wall sliding, and **jumping** (Z); blocks up to one unit high can be climbed
- **Level select menu** at startup when more than one level is found, and a **pause menu** on HOME (switch level / return to the Wii menu)
- Draw-distance culling and fog so big levels stay fast
- Built-in bitmap font, so the menus need no font files
- On-screen error screens for the usual problems (no SD card, no levels, textures missing, no Nunchuk)

**Assets**
- PNG textures in `assets/` are **compiled into the program** by `make`
- Level files **name the assets they use** (`TEX 0 brick`), nothing else has to be on the SD card

**Tools** (Python 3)
- `tools/level_editor.py`: graphical level editor
- `tools/embed_assets.py`: turns `assets/` into C data (run automatically by `make`)
- `tools/make_banner.py`: builds the banner, icon and sound data (`00000000.app`) for a channel / forwarder
- `tools/make_placeholder_assets.py`: generates sample textures and three sample levels, including a 64 × 64 maze

## Requirements

| For | You need |
|---|---|
| Building the game | [devkitPro](https://devkitpro.org/wiki/Getting_Started) with devkitPPC and libogc, **GRRLIB 4.6.x**, and the ppc `libpng`, `freetype`, `libjpeg`, `libfat` packages |
| Level editor | Python 3 with Tkinter (`sudo apt install python3-tk`); Pillow optional for texture thumbnails |
| Banner tool | Python 3 and Pillow (`pip install pillow`); `ffmpeg` optional for non-WAV sound |
| Running | A Wii with the Homebrew Channel, or the [Dolphin](https://dolphin-emu.org/) emulator |

### Installing the toolchain (Ubuntu)

```bash
# 1. devkitPro (follow the "devkitPro pacman" instructions on the devkitPro wiki), then:
sudo dkp-pacman -Syu
sudo dkp-pacman -S wii-dev ppc-libpng ppc-freetype ppc-libjpeg-turbo ppc-bzip2 ppc-brotli libfat-ogc

# 2. environment (put these in ~/.bashrc too)
export DEVKITPRO=/opt/devkitpro
export DEVKITPPC=/opt/devkitpro/devkitPPC

# 3. GRRLIB, built from source (this also installs pngu)
git clone https://github.com/GRRLIB/GRRLIB.git
cd GRRLIB && make clean all install
```

GRRLIB 4.4+ installs into `$DEVKITPRO/portlibs/wii`; the Makefile already looks there.

## Quick start

```bash
git clone https://github.com/WiiDev-Team/BrickEngine-GX.git
cd BrickEngine-GX

python3 tools/make_placeholder_assets.py   # optional: sample textures + levels
make                                       # produces boot.dol

# copy to the SD card
mkdir -p /path/to/SD/apps/wiigame
cp boot.dol /path/to/SD/apps/wiigame/
cp -r wiigame/levels /path/to/SD/apps/wiigame/
```

The game expects this layout on the card:

```
SD:/apps/brickengine-gx/
