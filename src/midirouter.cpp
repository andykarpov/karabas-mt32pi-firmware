//
// midirouter.cpp
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

#include "midirouter.h"

#ifndef UNIT_TEST
#include "synth/synthbase.h"
#else
#include "synthbase_stub.h"
#endif

CMIDIRouter::CMIDIRouter()
	: m_bEnabled(false),
	  m_Preset(TRouterPreset::SingleMT32),
	  m_pMT32(nullptr),
	  m_pFluidSynth(nullptr),
	  m_pYmfm(nullptr)
{
	for (unsigned i = 0; i < NumChannels; ++i)
	{
		m_pChannelMap[i] = nullptr;
		m_nChannelRemap[i] = static_cast<u8>(i);  // identity mapping
		m_bLayered[i] = false;
		m_fChannelVolume[i] = 1.0f;
	}
	ResetCCFilters();
}

void CMIDIRouter::SetChannelEngine(u8 nChannel, CSynthBase* pEngine)
{
	if (nChannel < NumChannels)
	{
		m_pChannelMap[nChannel] = pEngine;
		m_Preset = TRouterPreset::Custom;
	}
}

void CMIDIRouter::SetAllChannels(CSynthBase* pEngine)
{
	for (unsigned i = 0; i < NumChannels; ++i)
		m_pChannelMap[i] = pEngine;
}

CSynthBase* CMIDIRouter::GetChannelEngine(u8 nChannel) const
{
	if (nChannel < NumChannels)
		return m_pChannelMap[nChannel];
	return nullptr;
}

void CMIDIRouter::ApplyPreset(TRouterPreset Preset)
{
	m_Preset = Preset;

	switch (Preset)
	{
		case TRouterPreset::SingleMT32:
			SetAllChannels(m_pMT32);
			break;

		case TRouterPreset::SingleFluid:
			SetAllChannels(m_pFluidSynth);
			break;

		case TRouterPreset::SingleYmfm:
			SetAllChannels(m_pYmfm);
			break;

		case TRouterPreset::SplitGM:
			// Channels 1-9 (indices 0-8) → MT-32
			for (unsigned i = 0; i < 9; ++i)
				m_pChannelMap[i] = m_pMT32;
			// Channels 10-16 (indices 9-15) → FluidSynth
			for (unsigned i = 9; i < NumChannels; ++i)
				m_pChannelMap[i] = m_pFluidSynth;
			break;

		case TRouterPreset::Custom:
			// Don't change the table — user manages it
			break;
	}
}

void CMIDIRouter::RouteShortMessage(u32 nMessage)
{
	const u8 status = nMessage & 0xFF;

	// System real-time and system common (>= 0xF0): broadcast to all registered engines
	if (status >= 0xF0)
	{
		BroadcastToEngines(nMessage);
		return;
	}

	const u8 nChannel = status & 0x0F;
	const u8 nMsgType = status & 0xF0;
	CSynthBase* pTarget = m_pChannelMap[nChannel];
	const u32 nRouted  = RemapMessage(nChannel, nMessage);

	if (nMsgType == 0xB0)
	{
		DispatchCC(nRouted, nChannel, pTarget);
		return;
	}

	if (m_bLayered[nChannel] && (nMsgType == 0x90 || nMsgType == 0x80))
	{
		BroadcastToEngines(nRouted);
		return;
	}

	if (pTarget)
		pTarget->HandleMIDIShortMessage(nRouted);
}

u32 CMIDIRouter::RemapMessage(u8 nChannel, u32 nMessage) const
{
	const u8 nRemapped = m_nChannelRemap[nChannel];
	if (nRemapped == nChannel)
		return nMessage;
	return (nMessage & 0xFFFFFF00u) | ((nMessage & 0xF0u) | nRemapped);
}

u32 CMIDIRouter::ScaleCC7(u32 nMessage, u8 nChannel) const
{
	const u8 nCC = static_cast<u8>((nMessage >> 8) & 0x7F);
	if (nCC != 7 || m_fChannelVolume[nChannel] >= 1.0f)
		return nMessage;
	const u8 nRaw    = static_cast<u8>((nMessage >> 16) & 0x7F);
	const u8 nScaled = static_cast<u8>(nRaw * m_fChannelVolume[nChannel] + 0.5f);
	return (nMessage & 0xFF00FFFFu) | (static_cast<u32>(nScaled) << 16);
}

void CMIDIRouter::DispatchCC(u32 nMessage, u8 nChannel, CSynthBase* pTarget)
{
	const u8  nCC       = static_cast<u8>((nMessage >> 8) & 0x7F);
	const u32 nToSend   = ScaleCC7(nMessage, nChannel);

	auto SendIfAllowed = [&](CSynthBase* pEng, unsigned nEngIdx)
	{
		if (pEng && m_bCCFilter[nEngIdx][nCC])
			pEng->HandleMIDIShortMessage(nToSend);
	};

	if (pTarget == m_pMT32)
		SendIfAllowed(m_pMT32, EngMT32);
	else if (pTarget == m_pFluidSynth)
		SendIfAllowed(m_pFluidSynth, EngFluid);

	if (m_bLayered[nChannel])
	{
		if (pTarget != m_pMT32 && m_pMT32)
			SendIfAllowed(m_pMT32, EngMT32);
		if (pTarget != m_pFluidSynth && m_pFluidSynth)
			SendIfAllowed(m_pFluidSynth, EngFluid);
	}
}

void CMIDIRouter::BroadcastToEngines(u32 nMessage)
{
	if (m_pMT32)
		m_pMT32->HandleMIDIShortMessage(nMessage);
	if (m_pFluidSynth && m_pFluidSynth != m_pMT32)
		m_pFluidSynth->HandleMIDIShortMessage(nMessage);
}

void CMIDIRouter::RouteSysEx(const u8* pData, size_t nSize)
{
	// Minimum valid SysEx: F0 <id> ... F7  (at least 3 bytes)
	if (!pData || nSize < 3)
		return;

	// Check manufacturer ID (byte after F0)
	const u8 nManufacturer = pData[1];

	// Roland SysEx (manufacturer 0x41) → MT-32
	if (nManufacturer == 0x41)
	{
		if (m_pMT32)
			m_pMT32->HandleMIDISysExMessage(pData, nSize);
		return;
	}

	// Non-realtime / realtime universal SysEx (0x7E, 0x7F) → both engines
	if (nManufacturer == 0x7E || nManufacturer == 0x7F)
	{
		if (m_pMT32)
			m_pMT32->HandleMIDISysExMessage(pData, nSize);
		if (m_pFluidSynth)
			m_pFluidSynth->HandleMIDISysExMessage(pData, nSize);
		return;
	}

	// All other SysEx → FluidSynth (GM/GS/XG)
	if (m_pFluidSynth)
		m_pFluidSynth->HandleMIDISysExMessage(pData, nSize);
}

void CMIDIRouter::CountEngines(unsigned& nMT32, unsigned& nFluid, unsigned& nYmfm) const
{
	nMT32 = nFluid = nYmfm = 0;
	for (unsigned i = 0; i < NumChannels; ++i)
	{
		if (m_pChannelMap[i] == m_pMT32)
			++nMT32;
		else if (m_pChannelMap[i] == m_pFluidSynth)
			++nFluid;
		else if (m_pChannelMap[i] == m_pYmfm)
			++nYmfm;
	}
}

bool CMIDIRouter::IsDualMode() const
{
	// A layered channel sends to both engines even if the channel map only
	// lists one engine — so any active layering means dual mode.
	if (HasAnyLayering())
		return true;

	unsigned nMT32, nFluid, nYmfm;
	CountEngines(nMT32, nFluid, nYmfm);
	unsigned nActive = (nMT32 > 0 ? 1u : 0u) + (nFluid > 0 ? 1u : 0u) + (nYmfm > 0 ? 1u : 0u);
	return nActive >= 2;
}

CSynthBase* CMIDIRouter::GetPrimaryEngine() const
{
	unsigned nMT32, nFluid, nYmfm;
	CountEngines(nMT32, nFluid, nYmfm);

	if (nMT32 >= nFluid && nMT32 >= nYmfm)
		return m_pMT32;
	if (nFluid >= nYmfm)
		return m_pFluidSynth;
	return m_pYmfm;
}

void CMIDIRouter::SetChannelRemap(u8 nSrcChannel, u8 nDstChannel)
{
	if (nSrcChannel < NumChannels && nDstChannel < NumChannels)
	{
		m_nChannelRemap[nSrcChannel] = nDstChannel;
		m_Preset = TRouterPreset::Custom;
	}
}

u8 CMIDIRouter::GetChannelRemap(u8 nSrcChannel) const
{
	if (nSrcChannel < NumChannels)
		return m_nChannelRemap[nSrcChannel];
	return nSrcChannel;
}

void CMIDIRouter::ResetChannelRemap()
{
	for (unsigned i = 0; i < NumChannels; ++i)
		m_nChannelRemap[i] = static_cast<u8>(i);
}

const char* CMIDIRouter::GetChannelEngineName(u8 nChannel) const
{
	if (nChannel >= NumChannels)
		return "none";

	CSynthBase* pEngine = m_pChannelMap[nChannel];
	if (!pEngine)
		return "none";
	if (pEngine == m_pMT32)
		return "MT-32";
	if (pEngine == m_pFluidSynth)
		return "FluidSynth";
	if (pEngine == m_pYmfm)
		return "OPL3";

	return pEngine->GetName();
}

// ---------------------------------------------------------------------------
// CC Filters
// ---------------------------------------------------------------------------

void CMIDIRouter::SetCCFilter(unsigned nEngine, u8 nCC, bool bAllow)
{
	if (nEngine < MaxEngines && nCC < NumCCs)
		m_bCCFilter[nEngine][nCC] = bAllow;
}

bool CMIDIRouter::GetCCFilter(unsigned nEngine, u8 nCC) const
{
	if (nEngine < MaxEngines && nCC < NumCCs)
		return m_bCCFilter[nEngine][nCC];
	return true;
}

void CMIDIRouter::ResetCCFilters()
{
	for (unsigned e = 0; e < MaxEngines; ++e)
		for (unsigned cc = 0; cc < NumCCs; ++cc)
			m_bCCFilter[e][cc] = true;
}

// ---------------------------------------------------------------------------
// Layering
// ---------------------------------------------------------------------------

void CMIDIRouter::SetLayering(u8 nChannel, bool bLayered)
{
	if (nChannel < NumChannels)
		m_bLayered[nChannel] = bLayered;
}

bool CMIDIRouter::GetLayering(u8 nChannel) const
{
	if (nChannel < NumChannels)
		return m_bLayered[nChannel];
	return false;
}

void CMIDIRouter::SetAllLayering(bool bLayered)
{
	for (unsigned i = 0; i < NumChannels; ++i)
		m_bLayered[i] = bLayered;
}

bool CMIDIRouter::HasAnyLayering() const
{
	for (unsigned i = 0; i < NumChannels; ++i)
		if (m_bLayered[i])
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Per-channel volume
// ---------------------------------------------------------------------------

void CMIDIRouter::SetChannelVolume(u8 nChannel, float fVolume)
{
	if (nChannel < NumChannels)
	{
		if (fVolume < 0.0f) fVolume = 0.0f;
		if (fVolume > 1.0f) fVolume = 1.0f;
		m_fChannelVolume[nChannel] = fVolume;
	}
}

float CMIDIRouter::GetChannelVolume(u8 nChannel) const
{
	if (nChannel < NumChannels)
		return m_fChannelVolume[nChannel];
	return 1.0f;
}

void CMIDIRouter::ResetChannelVolumes()
{
	for (unsigned i = 0; i < NumChannels; ++i)
		m_fChannelVolume[i] = 1.0f;
}
