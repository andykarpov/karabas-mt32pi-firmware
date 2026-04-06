//
// yamahasysex.h
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

#ifndef _yamahasysex_h
#define _yamahasysex_h

#include <cstddef>

#include "synth/sysex.h"

enum TYamahaModelID : u8
{
	XG = 0x4C,
};

enum TYamahaAddress : u32
{
	XGSystemOn = 0x00007E,

	DisplayLetter = 0x060000,
	DisplayBitmap = 0x070000,
};

enum TYamahaAddressMask : u32
{
	DisplayLetterMask = 0xFFFF00,
};

struct TYamahaSysExHeader
{
	TManufacturerID ManufacturerID;
	TDeviceID DeviceID;
	TYamahaModelID ModelID;
	u8 Address[3];
};

// Result returned by ParseYamahaSysEx; display pointers refer into the original
// SysEx buffer (valid only during HandleMIDISysExMessage).
struct TYamahaSysExResult
{
	bool       bValid       = false;  // message was recognized
	bool       bConsume     = false;  // do not forward to FluidSynth
	bool       bReset       = false;  // XG System On (reset request)
	bool       bDisplayText = false;
	bool       bDisplayDots = false;
	const u8*  pDisplayData = nullptr;
	size_t     nDisplaySize = 0;
	u8         nAddressLo   = 0;
};

// Parse a SysEx message and return a result struct; no side effects.
TYamahaSysExResult ParseYamahaSysEx(const u8* pData, size_t nSize);

#endif
