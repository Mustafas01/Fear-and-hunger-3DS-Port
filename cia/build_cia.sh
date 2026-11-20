#!/bin/sh
# build_cia.sh - turns fear_map.elf into fear_map.cia for FBI.
# Run from the project folder (the one with the Makefile), in the devkitPro
# MSYS shell, after `make`:   sh cia/build_cia.sh
#
# Needs two tools next to this script or on PATH:
#   makerom    (from the Project_CTR project on GitHub)
#   bannertool (on GitHub; several maintained forks exist)
set -e
cd "$(dirname "$0")/.."
TOOLS="$(pwd)/cia"
export PATH="$TOOLS:$PATH"

ELF=fear_map.elf
[ -f "$ELF" ] || { echo "fear_map.elf not found - run make first"; exit 1; }
command -v makerom >/dev/null    || { echo "makerom not found (put makerom.exe in cia/)"; exit 1; }
command -v bannertool >/dev/null || { echo "bannertool not found (put bannertool.exe in cia/)"; exit 1; }

mkdir -p romfs   # makerom expects a RomFS folder (it can stay empty)
bannertool makebanner -i cia/banner.png -a cia/silence.wav -o cia/banner.bnr
bannertool makesmdh -s "F&H 3DS port" -l "Personal homebrew port" -p "homebrew" \
    -i cia/icon.png -o cia/icon.icn

# Unique ID is in the homebrew range (0xFF000-0xFFFFF); change it if it
# clashes with another title you have installed.
makerom -f cia -o fear_map.cia -target t -exefslogo \
    -elf "$ELF" -rsf cia/app.rsf -icon cia/icon.icn -banner cia/banner.bnr \
    -DAPP_TITLE="FHPort" -DAPP_PRODUCT_CODE="CTR-H-FHPT" \
    -DAPP_UNIQUE_ID=0xFF3A7 -DAPP_ENCRYPTED=false -DAPP_USE_ON_SD=true \
    -DAPP_CATEGORY=Application -DAPP_ROMFS=romfs \
    -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy \
    -DAPP_MEMORY_TYPE=Application -DAPP_CPU_SPEED=268MHz -DAPP_VERSION_MAJOR=1 \
    -DAPP_ENABLE_L2_CACHE=false

echo "built fear_map.cia"#!/bin/sh
# build_cia.sh - turns fear_map.elf into fear_map.cia for FBI.
# Run from the project folder (the one with the Makefile), in the devkitPro
# MSYS shell, after `make`:   sh cia/build_cia.sh
#
# Needs two tools next to this script or on PATH:
#   makerom    (from the Project_CTR project on GitHub)
#   bannertool (on GitHub; several maintained forks exist)
set -e
cd "$(dirname "$0")/.."
TOOLS="$(pwd)/cia"
export PATH="$TOOLS:$PATH"

ELF=fear_map.elf
[ -f "$ELF" ] || { echo "fear_map.elf not found - run make first"; exit 1; }
command -v makerom >/dev/null    || { echo "makerom not found (put makerom.exe in cia/)"; exit 1; }
command -v bannertool >/dev/null || { echo "bannertool not found (put bannertool.exe in cia/)"; exit 1; }

mkdir -p romfs   # makerom expects a RomFS folder (it can stay empty)
bannertool makebanner -i cia/banner.png -a cia/silence.wav -o cia/banner.bnr
bannertool makesmdh -s "F&H 3DS port" -l "Personal homebrew port" -p "homebrew" \
    -i cia/icon.png -o cia/icon.icn

# Unique ID is in the homebrew range (0xFF000-0xFFFFF); change it if it
# clashes with another title you have installed.
makerom -f cia -o fear_map.cia -target t -exefslogo \
    -elf "$ELF" -rsf cia/app.rsf -icon cia/icon.icn -banner cia/banner.bnr \
    -DAPP_TITLE="FHPort" -DAPP_PRODUCT_CODE="CTR-H-FHPT" \
    -DAPP_UNIQUE_ID=0xFF3A7 -DAPP_ENCRYPTED=false -DAPP_USE_ON_SD=true \
    -DAPP_CATEGORY=Application -DAPP_ROMFS=romfs \
    -DAPP_SYSTEM_MODE=64MB -DAPP_SYSTEM_MODE_EXT=Legacy \
    -DAPP_MEMORY_TYPE=Application -DAPP_CPU_SPEED=268MHz -DAPP_VERSION_MAJOR=1 \
    -DAPP_ENABLE_L2_CACHE=false

echo "built fear_map.cia"