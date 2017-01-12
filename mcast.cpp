
#include <unistd.h>
#include <syslog.h>
#include <netinet/ip.h>
#include <string>
#include <vector>
#include <limits>

class MulticastRefCount {
 public:
    uint32_t addr;
    uint32_t refCount;

    MulticastRefCount() : addr(0), refCount(0) { }
    MulticastRefCount(uint32_t addr) : addr(addr), refCount(1) { }
};

typedef std::vector<MulticastRefCount> MulticastRefVector;
static MulticastRefVector mcRef;

uint32_t countMulticastGroup(uint32_t addr)
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

bool joinMulticastGroup(int socket, uint32_t addr)
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
    mreq.imr_multiaddr.s_addr = htonl(addr);
    if (-1 == setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
	syslog(LOG_ERR, "Couldn't join multicast group: %d.%d.%d.%d -- %m", addr >> 24,
	       (uint8_t) (addr >> 16), (uint8_t) (addr >> 8), (uint8_t) addr);
    else {
	MulticastRefCount mcRefTmp(addr);

	mcRef.push_back(mcRefTmp);
#ifdef DEBUG
	syslog(LOG_DEBUG, "Joined multicast group: %d.%d.%d.%d", addr >> 24,
	       (uint8_t) (addr >> 16), (uint8_t) (addr >> 8), (uint8_t) addr);
#endif
	return true;
    }

    return false;
}

void dropMulticastGroup(int socket, uint32_t addr)
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
		mreq.imr_multiaddr.s_addr = htonl(addr);
		if (-1 == setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq)))
		    syslog(LOG_ERR, "Couldn't drop multicast group: %d.%d.%d.%d -- %m", addr >> 24,
				(uint8_t) (addr >> 16), (uint8_t) (addr >> 8), (uint8_t) addr);
#ifdef DEBUG
		else
		    syslog(LOG_DEBUG, "Dropped multicast group: %d.%d.%d.%d", addr >> 24,
			   (uint8_t) (addr >> 16), (uint8_t) (addr >> 8), (uint8_t) addr);
#endif
		mcRef.erase(ii);
	    }

	    return;
	} else
	    ii++;
    }
}

