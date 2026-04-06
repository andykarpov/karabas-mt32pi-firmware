//
// midirecorder.h
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

#ifndef _midirecorder_h
#define _midirecorder_h

#include <circle/types.h>
#include <fatfs/ff.h>

//
// Records live MIDI events to a Standard MIDI File (Type 0) on the SD card.
//
// Usage:
//   Start() — allocates buffer, emits SMF header + tempo meta event.
//   RecordShortMessage() / RecordSysEx() — append timestamped events in RAM.
//   Stop()  — writes End-of-Track, back-fills track length, flushes to SD.
//
// File naming: SD:recording_001.mid … SD:recording_999.mid (auto-increment).
// All events are kept in a fixed RAM buffer (MaxBufSize). When the buffer
// is nearly full the recording stops gracefully.
//
class CMidiRecorder
{
public:
        // MIDI timing constants
        static constexpr u16 PPQN    = 480;
        static constexpr u32 TempoUS = 500000u; // 120 BPM in µs/beat

        // Maximum in-memory recording buffer (~100 K short events)
        static constexpr u32 MaxBufSize = 256u * 1024u;

        CMidiRecorder();
        ~CMidiRecorder();

        // Begin recording. Returns false if already recording or alloc fails.
        bool Start();

        // Flush buffer to SD card and release memory.
        void Stop();

        bool IsRecording() const { return m_bRecording; }

        // Feed a parsed short message. nTicks = CTimer::GetClockTicks().
        // Active-sensing (0xFE) and system real-time bytes are silently ignored.
        void RecordShortMessage(u32 nMessage, unsigned nTicks);

        // Feed a SysEx message. pData[0]=0xF0, pData[nSize-1]=0xF7.
        void RecordSysEx(const u8* pData, size_t nSize, unsigned nTicks);

private:
        void WriteUint32BE(u32 v);
        void WriteUint16BE(u16 v);
        void WriteVarLen(u32 v);
        void WriteByte(u8 b);
        void WriteBytes(const u8* pData, u32 nLen);

        // Returns delta MIDI ticks since last event and advances m_nLastTicks.
        u32 DeltaTicks(unsigned nNow);

        bool     m_bRecording;
        bool     m_bFirstEvent;   // true until the first event anchors m_nLastTicks
        char     m_szPath[128];
        unsigned m_nLastTicks;    // CTimer::GetClockTicks() at last event

        u8*      m_pBuf;
        u32      m_nBufPos;
        unsigned m_nNextSlot;  // Next slot search starts here (O(1) amortised)
};

#endif // _midirecorder_h
