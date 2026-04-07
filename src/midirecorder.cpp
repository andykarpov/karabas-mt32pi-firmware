//
// midirecorder.cpp
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

#include <circle/logger.h>
#include <fatfs/ff.h>

#include "midirecorder.h"

LOGMODULE("midirecorder");

// ---------------------------------------------------------------------------
// SMF Type 0 layout (offsets in the output buffer)
// ---------------------------------------------------------------------------
//  0 ..  3  "MThd"
//  4 ..  7  0x00000006   (header chunk length)
//  8 ..  9  0x0000       (format 0)
// 10 .. 11  0x0001       (1 track)
// 12 .. 13  0x01E0       (PPQN = 480)
// 14 .. 17  "MTrk"
// 18 .. 21  0x????????   (track chunk length — filled by Stop())
// 22 ..     track data   (tempo meta, events, End-of-Track)

static constexpr u32 kTrackLenOffset  = 18u;
static constexpr u32 kTrackDataStart  = 22u;

// ---------------------------------------------------------------------------

CMidiRecorder::CMidiRecorder()
        : m_bRecording(false)
        , m_bFirstEvent(true)
        , m_nLastTicks(0)
        , m_pBuf(new u8[MaxBufSize])  // pre-allocate once; avoids alloc on the hot path
        , m_nBufPos(0)
        , m_nNextSlot(1)
{
        m_szPath[0] = '\0';
}

CMidiRecorder::~CMidiRecorder()
{
        if (m_bRecording)
                Stop();
        delete[] m_pBuf;
}

bool CMidiRecorder::Start()
{
        if (m_bRecording)
                return false;

        if (!m_pBuf)
        {
                LOGERR("CMidiRecorder: buffer unavailable");
                return false;
        }

        // Find next unused slot starting from m_nNextSlot; avoids re-scanning
        // all 999 slots on repeated Start/Stop cycles.
        bool bFound = false;
        for (int offset = 0; offset < 999; ++offset)
        {
                const int i = ((static_cast<int>(m_nNextSlot) - 1 + offset) % 999) + 1;
                __builtin_snprintf(m_szPath, sizeof(m_szPath),
                        "SD:recording_%03d.mid", i);
                FILINFO fno;
                if (f_stat(m_szPath, &fno) != FR_OK)
                {
                        m_nNextSlot = static_cast<unsigned>(i % 999) + 1;
                        bFound = true;
                        break;
                }
        }
        if (!bFound)
        {
                // All 999 slots used — overwrite the last
                __builtin_snprintf(m_szPath, sizeof(m_szPath),
                        "SD:recording_999.mid");
        }

        m_nBufPos    = 0;
        m_bFirstEvent = true;
        m_nLastTicks  = 0;

        // --- MThd chunk ---
        WriteBytes(reinterpret_cast<const u8*>("MThd"), 4);
        WriteUint32BE(6u);          // header length
        WriteUint16BE(0u);          // format 0
        WriteUint16BE(1u);          // 1 track
        WriteUint16BE(PPQN);

        // --- MTrk chunk header (length placeholder at kTrackLenOffset) ---
        WriteBytes(reinterpret_cast<const u8*>("MTrk"), 4);
        WriteUint32BE(0u);          // placeholder — filled by Stop()

        // --- Tempo meta event (delta = 0, 120 BPM) ---
        WriteByte(0x00);            // delta = 0
        WriteByte(0xFF);            // meta event
        WriteByte(0x51);            // Set Tempo
        WriteByte(0x03);            // 3 data bytes
        WriteByte(static_cast<u8>((TempoUS >> 16) & 0xFF));
        WriteByte(static_cast<u8>((TempoUS >> 8)  & 0xFF));
        WriteByte(static_cast<u8>(TempoUS & 0xFF));

        m_bRecording = true;
        LOGNOTE("MIDI recording started: %s", m_szPath);
        return true;
}

void CMidiRecorder::Stop()
{
        if (!m_bRecording)
                return;

        m_bRecording = false;

        // End-of-Track meta event (delta = 0)
        WriteByte(0x00);
        WriteByte(0xFF);
        WriteByte(0x2F);
        WriteByte(0x00);

        // Back-fill track chunk length
        const u32 nTrackLen = m_nBufPos - kTrackDataStart;
        m_pBuf[kTrackLenOffset]     = static_cast<u8>((nTrackLen >> 24) & 0xFF);
        m_pBuf[kTrackLenOffset + 1] = static_cast<u8>((nTrackLen >> 16) & 0xFF);
        m_pBuf[kTrackLenOffset + 2] = static_cast<u8>((nTrackLen >> 8)  & 0xFF);
        m_pBuf[kTrackLenOffset + 3] = static_cast<u8>(nTrackLen & 0xFF);

        // Write to SD card
        FIL f;
        if (f_open(&f, m_szPath, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK)
        {
                UINT nWritten = 0;
                if (f_write(&f, m_pBuf, m_nBufPos, &nWritten) != FR_OK
                        || nWritten != m_nBufPos)
                {
                        LOGERR("CMidiRecorder: write failed for %s", m_szPath);
                }
                else
                {
                        LOGNOTE("MIDI recording saved: %s (%u bytes)",
                                m_szPath, static_cast<unsigned>(m_nBufPos));
                }
                f_close(&f);
        }
        else
        {
                LOGERR("CMidiRecorder: f_open failed for %s", m_szPath);
        }

        m_nBufPos = 0;
}

void CMidiRecorder::RecordShortMessage(u32 nMessage, unsigned nTicks)
{
        if (!m_bRecording)
                return;

        const u8 status = nMessage & 0xFF;
        const u8 type   = status & 0xF0;

        // Skip system real-time and other 0xFx messages (SysEx handled separately)
        if (status >= 0xF0)
                return;

        // Guard: need up to 8 bytes (4-byte VarLen + status + 2 data + margin)
        if (m_nBufPos + 8u > MaxBufSize)
        {
                LOGWARN("CMidiRecorder: buffer full, stopping");
                Stop();
                return;
        }

        if (m_bFirstEvent)
        {
                m_nLastTicks  = nTicks;
                m_bFirstEvent = false;
        }

        WriteVarLen(DeltaTicks(nTicks));
        WriteByte(status);
        WriteByte(static_cast<u8>((nMessage >> 8)  & 0xFF)); // data1

        // Program Change (0xCx) and Channel Pressure (0xDx) are 2-byte messages
        if (type != 0xC0 && type != 0xD0)
                WriteByte(static_cast<u8>((nMessage >> 16) & 0xFF)); // data2
}

void CMidiRecorder::RecordSysEx(const u8* pData, size_t nSize, unsigned nTicks)
{
        if (!m_bRecording || nSize < 2)
                return;

        // Guard: VarLen(4) + 0xF0(1) + VarLen(4) + data
        if (m_nBufPos + 10u + nSize > MaxBufSize)
        {
                LOGWARN("CMidiRecorder: buffer full, stopping");
                Stop();
                return;
        }

        if (m_bFirstEvent)
        {
                m_nLastTicks  = nTicks;
                m_bFirstEvent = false;
        }

        // SMF SysEx: <delta> F0 <var-len of bytes after F0> <data including F7>
        const u32 nPayload = static_cast<u32>(nSize - 1); // bytes after the F0
        WriteVarLen(DeltaTicks(nTicks));
        WriteByte(0xF0);
        WriteVarLen(nPayload);
        WriteBytes(pData + 1, nPayload);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

u32 CMidiRecorder::DeltaTicks(unsigned nNow)
{
        const unsigned nDeltaUs = nNow - m_nLastTicks;
        m_nLastTicks = nNow;
        // ticks = delta_us * PPQN / TempoUS  (round nearest)
        return static_cast<u32>(
                (static_cast<unsigned long long>(nDeltaUs) * PPQN
                        + TempoUS / 2u)
                / TempoUS);
}

void CMidiRecorder::WriteUint32BE(u32 v)
{
        const u8 buf[4] = {
                static_cast<u8>((v >> 24) & 0xFF),
                static_cast<u8>((v >> 16) & 0xFF),
                static_cast<u8>((v >> 8)  & 0xFF),
                static_cast<u8>(v         & 0xFF),
        };
        WriteBytes(buf, 4);
}

void CMidiRecorder::WriteUint16BE(u16 v)
{
        const u8 buf[2] = {
                static_cast<u8>((v >> 8) & 0xFF),
                static_cast<u8>(v        & 0xFF),
        };
        WriteBytes(buf, 2);
}

void CMidiRecorder::WriteVarLen(u32 v)
{
        // Encode groups of 7 bits LSB-first into a temporary buffer,
        // then write them MSB-first with continuation bits on all but the last.
        u8 buf[4];
        int n = 0;

        buf[n++] = static_cast<u8>(v & 0x7F);
        v >>= 7;
        while (v > 0)
        {
                buf[n++] = static_cast<u8>((v & 0x7F) | 0x80);
                v >>= 7;
        }

        for (int i = n - 1; i >= 0; --i)
                WriteByte(buf[i]);
}

void CMidiRecorder::WriteByte(u8 b)
{
        if (m_pBuf && m_nBufPos < MaxBufSize)
                m_pBuf[m_nBufPos++] = b;
}

void CMidiRecorder::WriteBytes(const u8* pData, u32 nLen)
{
        for (u32 i = 0; i < nLen; ++i)
                WriteByte(pData[i]);
}
