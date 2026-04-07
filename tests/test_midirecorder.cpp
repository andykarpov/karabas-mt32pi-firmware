//
// test_midirecorder.cpp
//
// Unit tests for CMidiRecorder.
//

#include "doctest/doctest.h"
#include "stubs/fatfs/ff.h"
#include "stubs/circle/timer.h"

#include "midirecorder.h"

#include <cstring>
#include <vector>

static void ResetFatFSStub()
{
	g_fatfs_open_fail = false;
	g_fatfs_read_fail = false;
	g_fatfs_stat_fail = true;
	g_fatfs_write_fail = false;
	g_fatfs_data = nullptr;
	g_fatfs_data_size = 0;
	g_fatfs_written_size = 0;
	g_fatfs_seek_pos = 0;
	std::memset(g_fatfs_written_buf, 0, kFatfsWriteBufSize);
	StubTimer::s_clock_ticks = 0;
}

static unsigned ReadBE32(const unsigned char* p)
{
	return (static_cast<unsigned>(p[0]) << 24)
		| (static_cast<unsigned>(p[1]) << 16)
		| (static_cast<unsigned>(p[2]) << 8)
		| static_cast<unsigned>(p[3]);
}

TEST_CASE("MidiRecorder: Start then Stop writes a valid empty Type 0 SMF")
{
	ResetFatFSStub();
	CMidiRecorder recorder;

	REQUIRE(recorder.Start());
	CHECK(recorder.IsRecording());

	recorder.Stop();
	CHECK_FALSE(recorder.IsRecording());

	REQUIRE(g_fatfs_written_size == 33u);
	CHECK(std::memcmp(g_fatfs_written_buf + 0,  "MThd", 4) == 0);
	CHECK(ReadBE32(g_fatfs_written_buf + 4) == 6u);
	CHECK(g_fatfs_written_buf[8] == 0x00);
	CHECK(g_fatfs_written_buf[9] == 0x00);
	CHECK(g_fatfs_written_buf[10] == 0x00);
	CHECK(g_fatfs_written_buf[11] == 0x01);
	CHECK(g_fatfs_written_buf[12] == 0x01);
	CHECK(g_fatfs_written_buf[13] == 0xE0);
	CHECK(std::memcmp(g_fatfs_written_buf + 14, "MTrk", 4) == 0);
	CHECK(ReadBE32(g_fatfs_written_buf + 18) == 11u);

	const unsigned char expectedTrack[] = {
		0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
		0x00, 0xFF, 0x2F, 0x00,
	};
	CHECK(std::memcmp(g_fatfs_written_buf + 22, expectedTrack, sizeof(expectedTrack)) == 0);
}

TEST_CASE("MidiRecorder: channel messages are written with delta times")
{
	ResetFatFSStub();
	CMidiRecorder recorder;

	REQUIRE(recorder.Start());

	StubTimer::s_clock_ticks = 1000000;
	recorder.RecordShortMessage(0x00643C90u, StubTimer::s_clock_ticks);

	StubTimer::s_clock_ticks = 1250000;
	recorder.RecordShortMessage(0x00003C80u, StubTimer::s_clock_ticks);

	recorder.Stop();

	const unsigned char expectedTrack[] = {
		0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
		0x00, 0x90, 0x3C, 0x64,
		0x81, 0x70, 0x80, 0x3C, 0x00,
		0x00, 0xFF, 0x2F, 0x00,
	};

	REQUIRE(ReadBE32(g_fatfs_written_buf + 18) == sizeof(expectedTrack));
	REQUIRE(g_fatfs_written_size == 22u + sizeof(expectedTrack));
	CHECK(std::memcmp(g_fatfs_written_buf + 22, expectedTrack, sizeof(expectedTrack)) == 0);
}

TEST_CASE("MidiRecorder: SysEx is emitted in SMF format")
{
	ResetFatFSStub();
	CMidiRecorder recorder;
	const unsigned char sysex[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };

	REQUIRE(recorder.Start());
	StubTimer::s_clock_ticks = 500000;
	recorder.RecordSysEx(sysex, sizeof(sysex), StubTimer::s_clock_ticks);
	recorder.Stop();

	const unsigned char expectedTrack[] = {
		0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
		0x00, 0xF0, 0x05, 0x7E, 0x7F, 0x09, 0x01, 0xF7,
		0x00, 0xFF, 0x2F, 0x00,
	};

	REQUIRE(ReadBE32(g_fatfs_written_buf + 18) == sizeof(expectedTrack));
	CHECK(std::memcmp(g_fatfs_written_buf + 22, expectedTrack, sizeof(expectedTrack)) == 0);
}

TEST_CASE("MidiRecorder: Start fails if already recording")
{
	ResetFatFSStub();
	CMidiRecorder recorder;

	REQUIRE(recorder.Start());
	CHECK_FALSE(recorder.Start());
	recorder.Stop();
}

TEST_CASE("MidiRecorder: Start succeeds before the output file is opened")
{
	ResetFatFSStub();
	CMidiRecorder recorder;

	g_fatfs_open_fail = true;
	CHECK(recorder.Start());
	recorder.Stop();
}

TEST_CASE("MidiRecorder: multiple consecutive Start/Stop cycles succeed")
{
	ResetFatFSStub();
	CMidiRecorder recorder;

	// First cycle
	REQUIRE(recorder.Start());
	CHECK(recorder.IsRecording());
	recorder.Stop();
	CHECK_FALSE(recorder.IsRecording());

	const size_t firstWriteSize = g_fatfs_written_size;

	// Second cycle — should work identically
	g_fatfs_written_size = 0;
	g_fatfs_seek_pos     = 0;
	std::memset(g_fatfs_written_buf, 0, firstWriteSize);

	REQUIRE(recorder.Start());
	CHECK(recorder.IsRecording());
	recorder.Stop();
	CHECK_FALSE(recorder.IsRecording());

	// Both empty SMFs must have the same size
	CHECK(g_fatfs_written_size == firstWriteSize);

	// Third cycle — also OK
	REQUIRE(recorder.Start());
	recorder.Stop();
	CHECK_FALSE(recorder.IsRecording());
}

TEST_CASE("MidiRecorder: buffer overflow stops recording gracefully")
{
	ResetFatFSStub();
	CMidiRecorder recorder;

	REQUIRE(recorder.Start());
	CHECK(recorder.IsRecording());

	// Build a SysEx large enough to trigger the overflow guard:
	// After Start(), m_nBufPos == kTrackDataStart + 7 (= 29 bytes).
	// RecordSysEx guard: m_nBufPos + 10 + nSize > MaxBufSize
	// → nSize > MaxBufSize - 39.  Use MaxBufSize to be clearly over threshold.
	const size_t kOverflowSize = CMidiRecorder::MaxBufSize;
	std::vector<u8> bigSysEx(kOverflowSize, 0x10);
	bigSysEx.front() = 0xF0;
	bigSysEx.back()  = 0xF7;

	// RecordSysEx internally calls Stop() when the buffer would overflow
	recorder.RecordSysEx(bigSysEx.data(), bigSysEx.size(), 1000);

	CHECK_FALSE(recorder.IsRecording());
}