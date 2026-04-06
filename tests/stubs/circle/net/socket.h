//
// circle/net/socket.h (test stub)
//
// Minimal stub for CSocket, for host-side unit tests.
//

#ifndef _circle_net_socket_stub_h
#define _circle_net_socket_stub_h

#include <circle/net/ipaddress.h>
#include <circle/netdevice.h>
#include <circle/types.h>

#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

class CNetSubSystem;

class CSocket
{
public:
	CSocket(CNetSubSystem* /*pNet*/, int /*nProtocol*/) {}
	virtual ~CSocket() {}
	int Bind(u16 /*nPort*/) const { return 0; }
	int Receive(void* /*pBuffer*/, unsigned /*nLength*/, int /*nFlags*/) const { return 0; }
	int ReceiveFrom(void* /*pBuffer*/, unsigned /*nLength*/, int /*nFlags*/,
	                CIPAddress* /*pAddr*/, u16* /*pPort*/) const { return -1; }
	int SendTo(const void* /*pBuffer*/, unsigned /*nLength*/, int /*nFlags*/,
	           CIPAddress /*addr*/, u16 /*nPort*/) const { return 0; }
	const CIPAddress* GetForeignIP() const { return nullptr; }
};

#endif
