#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
	controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
	controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
	controlfolder="$XDG_DATA_HOME/PortMaster"
else
	controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/jazz2"
GAME="jazz2.${DEVICE_ARCH}"

# "Content" is resolved relative to the working directory (`NCINE_PACKAGED_CONTENT_PATH`)
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

$ESUDO chmod +x "$GAMEDIR/$GAME"

# "Source", "Cache" and "Jazz2.config" are stored in "ports/jazz2/" (`NCINE_LINUX_PACKAGE=jazz2`)
export XDG_CONFIG_HOME="/$directory/ports"
export XDG_DATA_HOME="/$directory/ports"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
# Libraries not provided by the firmware
export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"

# gptokeyb only provides the exit hotkey, the game reads the controller through SDL
$GPTOKEYB2 "$GAME" &>/dev/null &
pm_platform_helper "$GAMEDIR/$GAME"
"./$GAME"

pm_finish
