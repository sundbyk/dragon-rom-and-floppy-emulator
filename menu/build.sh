#!/bin/sh
# Run from the menu folder

# Count the build number up. It goes after the version in the menu's title, eg. "MENU 2.2.11"
n=$(( $(cat build_number 2>/dev/null || echo 0) + 1 ))
echo $n > build_number
echo "	FCC	/$n/" > build.inc

# menu.rom is padded to 8K by menu.s itself. Copy it to the root of the SD card.
lwasm -f raw --list=menu.lst -o menu.rom menu.s

# Same menu with a built in list of names, for trying it out in an emulator such as XRoar
lwasm -f raw -D SIM -o menu-sim.rom menu.s
