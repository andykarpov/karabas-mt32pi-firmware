//
// rolandsysex.h
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2023 Dale Whinham <daleyo@gmail.com>
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef _rolandsysex_h
#define _rolandsysex_h

#include <cstddef>

#include "synth/sysex.h"

enum TRolandModelID : u8
{
	GS   = 0x42,
	SC55 = 0x45
};

enum TRolandCommandID : u8
{
	RQ1 = 0x11,
	DT1 = 0x12,
};

enum TRolandAddress : u32
{
	SystemModeSet = 0x00007F,

	GSReset          = 0x40007F,
	UseForRhythmPart = 0x401015,

	SC55DisplayText = 0x100000,
	SC55DisplayDots = 0x100100,
};

enum TRolandAddressMask : u32
{
	PatchPart = 0xFFF0FF,
};

struct TRolandSysExHeader
{
	TManufacturerID ManufacturerID;
	TDeviceID DeviceID;
	TRolandModelID ModelID;
	TRolandCommandID CommandID;
	u8 Address[3];
};

// Result returned by ParseRolandSysEx; display pointers refer into the original
// SysEx buffer (valid only during HandleMIDISysExMessage).
struct TRolandSysExResult
{
	bool       bValid       = false;  // message was recognized and checksum OK
	bool       bConsume     = false;  // do not forward to FluidSynth
	bool       bReset       = false;  // GS Reset or SystemModeSet
	bool       bPercChange  = false;  // percussion channel mode change
	u8         nPercChannel = 0;
	u8         nPercMode    = 0;
	bool       bDisplayText = false;
	bool       bDisplayDots = false;
	const u8*  pDisplayData = nullptr;
	size_t     nDisplaySize = 0;
	u8         nAddressLo   = 0;
};

// Parse a SysEx message and return a result struct; no side effects.
TRolandSysExResult ParseRolandSysEx(const u8* pData, size_t nSize);

#endif
