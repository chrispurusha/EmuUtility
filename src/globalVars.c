/*
 * The EmuUtility application.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "sysIncludes.h"
#include "types.h"
#include "globalVars.h"

_Atomic bool     gQuitAll        = false;
_Atomic bool     gReDraw         = true;

void *           gWindow         = NULL;
//double           gGlobalGuiScale = 1.0;
//tScrollState     gScrollState    = {0};

tEmuDevice       gDevice         = {0};
MIDIClientRef    gMidiClient     = 0;
MIDIPortRef      gMidiInPort     = 0;
MIDIPortRef      gMidiOutPort    = 0;
MIDIEndpointRef  gMidiSource     = 0;
MIDIEndpointRef  gMidiDest       = 0;

_Atomic bool     gSessionOpen    = false;
_Atomic uint8_t  gSessionSeqId   = 0;

tLcdBuffer       gLcd            = {0};
_Atomic bool     gNeedLcdFull    = true;
_Atomic bool     gNeedLcdDelta   = false;
_Atomic bool     gLcdPending     = false;

_Atomic uint32_t gLeds           = 0;
_Atomic bool     gNeedLeds       = true;
