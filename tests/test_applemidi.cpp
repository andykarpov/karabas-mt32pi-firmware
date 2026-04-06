//
// test_applemidi.cpp
//
// Host-side unit tests for AppleMIDI free parsing functions.
//
// Covers ParseInvitationPacket, ParseEndSessionPacket, and ParseSyncPacket
// which are pure packet-parsing logic with no Circle runtime dependencies.
//

#include "doctest/doctest.h"

#include <circle/macros.h>
#include <circle/types.h>
#include "net/byteorder.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Internal types mirrored from applemidi.cpp.
// Definitions are identical in layout so forward declarations link correctly.
// ---------------------------------------------------------------------------

static constexpr size_t kMaxNameLength     = 256;
static constexpr size_t kNamelessPktSize   = 2 + 2 + 4 + 4 + 4; // 16 bytes
static constexpr size_t kSyncPktSize       = 2 + 2 + 4 + 1 + 3 + 8 * 3; // 36 bytes

struct TAppleMIDISession
{
	u16  nSignature;
	u16  nCommand;
	u32  nVersion;
	u32  nInitiatorToken;
	u32  nSSRC;
	char Name[kMaxNameLength];
}
PACKED;

struct TAppleMIDISync
{
	u16 nSignature;
	u16 nCommand;
	u32 nSSRC;
	u8  nCount;
	u8  Padding[3];
	u64 Timestamps[3];
}
PACKED;

// Forward-declare free functions from applemidi.cpp
bool ParseInvitationPacket(const u8* pBuffer, size_t nSize, TAppleMIDISession* pOutPacket);
bool ParseEndSessionPacket(const u8* pBuffer, size_t nSize, TAppleMIDISession* pOutPacket);
bool ParseSyncPacket(const u8* pBuffer, size_t nSize, TAppleMIDISync* pOutPacket);

// ---------------------------------------------------------------------------
// Byte-writing helpers (big-endian / network byte order)
// ---------------------------------------------------------------------------

static void WriteU16BE(u8* p, u16 val)
{
	p[0] = static_cast<u8>(val >> 8);
	p[1] = static_cast<u8>(val);
}

static void WriteU32BE(u8* p, u32 val)
{
	p[0] = static_cast<u8>(val >> 24);
	p[1] = static_cast<u8>(val >> 16);
	p[2] = static_cast<u8>(val >> 8);
	p[3] = static_cast<u8>(val);
}

static void WriteU64BE(u8* p, u64 val)
{
	p[0] = static_cast<u8>(val >> 56);
	p[1] = static_cast<u8>(val >> 48);
	p[2] = static_cast<u8>(val >> 40);
	p[3] = static_cast<u8>(val >> 32);
	p[4] = static_cast<u8>(val >> 24);
	p[5] = static_cast<u8>(val >> 16);
	p[6] = static_cast<u8>(val >> 8);
	p[7] = static_cast<u8>(val);
}

// Build a valid nameless invitation packet into buf[kNamelessPktSize]
static void MakeInvitationBuf(u8* buf)
{
	memset(buf, 0, kNamelessPktSize);
	WriteU16BE(buf + 0,  0xFFFF);          // signature
	WriteU16BE(buf + 2,  0x494E);          // command "IN"
	WriteU32BE(buf + 4,  2);               // version 2
	WriteU32BE(buf + 8,  0xABCD1234u);     // initiatorToken
	WriteU32BE(buf + 12, 0x00000001u);     // SSRC
}

// ---------------------------------------------------------------------------
// ParseInvitationPacket tests
// ---------------------------------------------------------------------------

TEST_CASE("AppleMIDI: ParseInvitationPacket succeeds for valid nameless packet")
{
	u8 buf[kNamelessPktSize];
	MakeInvitationBuf(buf);
	TAppleMIDISession out;
	CHECK(ParseInvitationPacket(buf, kNamelessPktSize, &out));
	const u16 sig = out.nSignature;
	const u32 ver = out.nVersion;
	const u32 tok = out.nInitiatorToken;
	const u32 ssrc = out.nSSRC;
	CHECK(sig  == 0xFFFF);
	CHECK(ver  == 2u);
	CHECK(tok  == 0xABCD1234u);
	CHECK(ssrc == 0x00000001u);
	CHECK(strcmp(out.Name, "<unknown>") == 0);
}

TEST_CASE("AppleMIDI: ParseInvitationPacket copies session name when present")
{
	u8 buf[kNamelessPktSize + 16] = {};
	MakeInvitationBuf(buf);
	const char* name = "GarageBand";
	memcpy(buf + kNamelessPktSize, name, strlen(name) + 1);
	TAppleMIDISession out;
	CHECK(ParseInvitationPacket(buf, kNamelessPktSize + strlen(name) + 1, &out));
	CHECK(strcmp(out.Name, name) == 0);
}

TEST_CASE("AppleMIDI: ParseInvitationPacket rejects buffer shorter than minimum")
{
	u8 buf[kNamelessPktSize];
	MakeInvitationBuf(buf);
	TAppleMIDISession out;
	CHECK_FALSE(ParseInvitationPacket(buf, kNamelessPktSize - 1, &out));
}

TEST_CASE("AppleMIDI: ParseInvitationPacket rejects wrong signature")
{
	u8 buf[kNamelessPktSize];
	MakeInvitationBuf(buf);
	WriteU16BE(buf + 0, 0xFFFE);  // corrupt signature
	TAppleMIDISession out;
	CHECK_FALSE(ParseInvitationPacket(buf, kNamelessPktSize, &out));
}

TEST_CASE("AppleMIDI: ParseInvitationPacket rejects unrecognised command")
{
	u8 buf[kNamelessPktSize];
	MakeInvitationBuf(buf);
	WriteU16BE(buf + 2, 0x4259);  // "BY" instead of "IN"
	TAppleMIDISession out;
	CHECK_FALSE(ParseInvitationPacket(buf, kNamelessPktSize, &out));
}

TEST_CASE("AppleMIDI: ParseInvitationPacket rejects wrong protocol version")
{
	u8 buf[kNamelessPktSize];
	MakeInvitationBuf(buf);
	WriteU32BE(buf + 4, 1);  // version 1 instead of 2
	TAppleMIDISession out;
	CHECK_FALSE(ParseInvitationPacket(buf, kNamelessPktSize, &out));
}

// ---------------------------------------------------------------------------
// ParseEndSessionPacket tests
// ---------------------------------------------------------------------------

TEST_CASE("AppleMIDI: ParseEndSessionPacket succeeds for valid BY packet")
{
	u8 buf[kNamelessPktSize] = {};
	WriteU16BE(buf + 0,  0xFFFF);          // signature
	WriteU16BE(buf + 2,  0x4259);          // command "BY"
	WriteU32BE(buf + 4,  2);               // version
	WriteU32BE(buf + 8,  0x0000CAFEu);     // initiatorToken
	WriteU32BE(buf + 12, 0x0000BABEu);     // SSRC
	TAppleMIDISession out;
	CHECK(ParseEndSessionPacket(buf, kNamelessPktSize, &out));
	const u32 ssrc = out.nSSRC;
	CHECK(ssrc == 0x0000BABEu);
}

TEST_CASE("AppleMIDI: ParseEndSessionPacket rejects invitation command")
{
	u8 buf[kNamelessPktSize] = {};
	WriteU16BE(buf + 0, 0xFFFF);
	WriteU16BE(buf + 2, 0x494E);  // "IN" — wrong for end-session
	WriteU32BE(buf + 4, 2);
	TAppleMIDISession out;
	CHECK_FALSE(ParseEndSessionPacket(buf, kNamelessPktSize, &out));
}

TEST_CASE("AppleMIDI: ParseEndSessionPacket rejects too-short buffer")
{
	u8 buf[kNamelessPktSize] = {};
	WriteU16BE(buf + 0, 0xFFFF);
	WriteU16BE(buf + 2, 0x4259);
	WriteU32BE(buf + 4, 2);
	TAppleMIDISession out;
	CHECK_FALSE(ParseEndSessionPacket(buf, kNamelessPktSize - 1, &out));
}

// ---------------------------------------------------------------------------
// ParseSyncPacket tests
// ---------------------------------------------------------------------------

TEST_CASE("AppleMIDI: ParseSyncPacket succeeds and copies all three timestamps")
{
	u8 buf[kSyncPktSize] = {};
	WriteU16BE(buf + 0,  0xFFFF);          // signature
	WriteU16BE(buf + 2,  0x434B);          // command "CK"
	WriteU32BE(buf + 4,  0xDEADBEEFu);     // SSRC
	buf[8]  = 1;                            // nCount
	// Padding[3] = 0 (already zero)
	WriteU64BE(buf + 12, 0x0000000000000100ULL);  // Timestamp[0]
	WriteU64BE(buf + 20, 0x0000000000000200ULL);  // Timestamp[1]
	WriteU64BE(buf + 28, 0x0000000000000300ULL);  // Timestamp[2]
	TAppleMIDISync out;
	CHECK(ParseSyncPacket(buf, kSyncPktSize, &out));
	const u32 ssrc = out.nSSRC;
	const u8  cnt  = out.nCount;
	const u64 ts0  = out.Timestamps[0];
	const u64 ts1  = out.Timestamps[1];
	const u64 ts2  = out.Timestamps[2];
	CHECK(ssrc == 0xDEADBEEFu);
	CHECK(cnt  == 1u);
	CHECK(ts0  == 0x100u);
	CHECK(ts1  == 0x200u);
	CHECK(ts2  == 0x300u);
}

TEST_CASE("AppleMIDI: ParseSyncPacket rejects buffer shorter than struct size")
{
	u8 buf[kSyncPktSize] = {};
	WriteU16BE(buf + 0, 0xFFFF);
	WriteU16BE(buf + 2, 0x434B);
	TAppleMIDISync out;
	CHECK_FALSE(ParseSyncPacket(buf, kSyncPktSize - 1, &out));
}

TEST_CASE("AppleMIDI: ParseSyncPacket rejects wrong signature")
{
	u8 buf[kSyncPktSize] = {};
	WriteU16BE(buf + 0, 0x0000);  // wrong signature
	WriteU16BE(buf + 2, 0x434B);
	WriteU32BE(buf + 4, 1);
	TAppleMIDISync out;
	CHECK_FALSE(ParseSyncPacket(buf, kSyncPktSize, &out));
}
