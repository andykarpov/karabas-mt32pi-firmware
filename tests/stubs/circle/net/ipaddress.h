//
// circle/net/ipaddress.h (test stub)
//
// Minimal stub for CIPAddress, for host-side unit tests.
//

#ifndef _circle_net_ipaddress_stub_h
#define _circle_net_ipaddress_stub_h

#include <cstdint>
#include "circle/string.h"

class CIPAddress
{
public:
	CIPAddress() : m_addr(0) {}

	void Set(const CIPAddress& other) { m_addr = other.m_addr; }
	void Format(CString* pStr) const { if (pStr) pStr->Format("0.0.0.0"); }

	bool operator==(const CIPAddress& o) const { return m_addr == o.m_addr; }
	bool operator!=(const CIPAddress& o) const { return m_addr != o.m_addr; }

private:
	uint32_t m_addr;
};

#endif
