//
// circle/bcmrandom.h (test stub)
//
// Minimal stub for CBcmRandomNumberGenerator, for host-side unit tests.
//

#ifndef _circle_bcmrandom_stub_h
#define _circle_bcmrandom_stub_h

#include <cstdint>

class CBcmRandomNumberGenerator
{
public:
	uint32_t GetNumber() { return 0x12345678u; }
};

#endif
