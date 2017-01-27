#include <unistd.h>
#include <syslog.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <limits>
#include "server.h"

class MulticastRefCount {
 public:
    ipaddr_t addr;
    uint32_t refCount;

    MulticastRefCount() : refCount(0) { }
    MulticastRefCount(ipaddr_t addr) : addr(addr), refCount(1) { }
};

typedef std::vector<MulticastRefCount> MulticastRefVector;
static MulticastRefVector mcRef;

uint32_t countMulticastGroup(ipaddr_t addr)
{
    auto ii = mcRef.begin();

    while (ii != mcRef.end()) {
	if ((*ii).addr == addr)
	    return (*ii).refCount;
	else
	    ii++;
    }

    return 0;
}

bool joinMulticastGroup(int socket, ipaddr_t addr)
{
    auto ii = mcRef.begin();

    // See if we are already a member of the multicast group 'addr'
    // and just increment the reference count if we are

    while (ii != mcRef.end()) {
        if ((*ii).addr == addr) {
            if ((*ii).refCount < (uint32_t) std::numeric_limits<uint32_t>::max()) {
                (*ii).refCount++;
                return true;
            } else
                return false;
        } else
	    ii++;
    }

    // At this point we know we are the first task to request this
    // multicast group so we add the multicast address to the
    // reference count vector and then join the group

    ip_mreq mreq;

    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    mreq.imr_multiaddr.s_addr = htonl(addr.value());
    if (-1 == setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
	syslog(LOG_ERR, "Couldn't join multicast group: %s -- %m", addr.str().c_str());
    else {
	MulticastRefCount mcRefTmp(addr);

	mcRef.push_back(mcRefTmp);
#ifdef DEBUG
	syslog(LOG_DEBUG, "Joined multicast group: %s", addr.str().c_str());
#endif
	return true;
    }

    return false;
}

void dropMulticastGroup(int socket, ipaddr_t addr)
{
    auto ii = mcRef.begin();

    while (ii != mcRef.end()) {
	if ((*ii).addr == addr) {

	    // When we decrement the reference count to zero then no
	    // other tasks need to be part of this group so we drop
	    // member ship and remove the multicast address from the
	    // reference count vector

	    if ((*ii).refCount)
		(*ii).refCount--;

	    if ((*ii).refCount == 0) {
		ip_mreq mreq;

		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		mreq.imr_multiaddr.s_addr = htonl(addr.value());
		if (-1 == setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)))
		    syslog(LOG_ERR, "Couldn't drop multicast group: %s -- %m", addr.str().c_str());
#ifdef DEBUG
		else
		    syslog(LOG_DEBUG, "Dropped multicast group: %s", addr.str().c_str());
#endif
		mcRef.erase(ii);
	    }

	    return;
	} else
	    ii++;
    }
}

