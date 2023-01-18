#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <cstring>
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "server.h"
#include <algorithm>

// Local types
//
static inline uint16_t swap(uint16_t v)
{
    return (v >> 8) + (v << 8);
}

class DataOut {
    trunknode_t tgt;
    size_t total;
    uint8_t data[INTERNAL_ACNET_PACKET_SIZE];

    void addData(void const* d, size_t const n) throw()
    {
#ifdef NO_SWAP
	memcpy(data + total, d, n);
	total += n;
#else
	uint8_t* ptr = data + total;
	size_t const end = n & ~1;

	// When we copy the new data to our outgoing buffer, we need to swap bytes.

	std::transform(reinterpret_cast<uint16_t const*>(d),
	      reinterpret_cast<uint16_t const*>(d) + end / 2,
	      reinterpret_cast<uint16_t*>(ptr),
	      swap);

	if (end != n) {
	    ptr[end] = 0;
	    ptr[end + 1] = ((uint8_t *) d)[n - 1]; 
	    total += n + 1;
	} else
	    total += n;
#endif
    }

 public:
    DataOut() : total(0) { }

    bool addData(AcnetHeader const& hdr, void const* d, size_t const n) throw()
    {
	assert(d || !n);

	if (((n + 1) & ~1) + sizeof(AcnetHeader) <= sizeof(data) - total) {
	    addData(&hdr, sizeof(AcnetHeader));
	    addData(d, n);
	    return true;
	}
	return false;
    }

    void init(trunknode_t n) throw()
    {
	tgt = n;
	total = 0;
    }

    trunknode_t getTarget() const { return tgt; }
    size_t getPacketSize() const { return total; }
    uint8_t const* getPacketData() const { return data; }
};

typedef QueueAdaptor< DataOut > DataQueue;
typedef std::auto_ptr<DataOut> DataOutPtr;

// Local data

bool dumpOutgoing = false;
int sNetwork = -1;
static DataQueue outgoing;

// Local prototypes

static DataOut* allocPacket(trunknode_t);

// Allocates a new network packet. The new packet is associated with the given target node.

static DataOut* allocPacket(trunknode_t tgt)
{
    DataOutPtr ptr(new DataOut);

    ptr->init(tgt);
    outgoing.push(ptr.get());
    setPartialBuffer(tgt, ptr.get());
    return ptr.release();
}

// This function initializes a datagram socket. It not only creates the socket, but binds it to a port and sets up the
// send and receive buffer sizes.

int allocSocket(uint32_t addr, uint16_t port, int szSnd, int szRcv)
{
    int const tmp = socket(AF_INET, SOCK_DGRAM, 0);

    if (-1 != tmp) {
	u_char ttl = 32;
	u_char loop = 1;

	// Set a large outgoing buffer and a decent size incoming buffer.

	if (-1 == setsockopt(tmp, SOL_SOCKET, SO_SNDBUF, &szSnd, sizeof(szSnd)))
	    syslog(LOG_WARNING, "couldn't set send buf size for network -- %m");
	if (-1 == setsockopt(tmp, SOL_SOCKET, SO_RCVBUF, &szRcv, sizeof(szRcv)))
	    syslog(LOG_WARNING, "couldn't set rcv buf size for network -- %m");
	if (-1 == setsockopt(tmp, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)))
	    syslog(LOG_WARNING, "couldn't set LOOPBACK for multicasts -- %m");
	if (-1 == setsockopt(tmp, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)))
	    syslog(LOG_WARNING, "couldn't set TTL for multicasts -- %m");

	if (-1 != fcntl(tmp, F_SETFL, O_NONBLOCK)) {

	    // Bind the socket to the requested port and address.

	    sockaddr_in in;

#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
	    in.sin_len = sizeof(in);
#endif
	    in.sin_family = AF_INET;
	    in.sin_port = htons(port);
	    in.sin_addr.s_addr = htonl(addr);

	    if (-1 != ::bind(tmp, reinterpret_cast<sockaddr*>(&in), sizeof(in)))
		return tmp;
	    else
		syslog(LOG_ERR, "couldn't bind() to port %d -- %m", port);
	} else
	    syslog(LOG_ERR, "couldn't set socket to non-blocking -- %m");
	close(tmp);
    } else
	syslog(LOG_ERR, "couldn't create socket -- %m");

    return -1;
}

int allocClientTcpSocket(uint32_t addr, uint16_t port, int szSnd, int szRcv)
{
    int const tmp = socket(AF_INET, SOCK_STREAM, 0);

    if (-1 != tmp) {

	// Set a large outgoing buffer and a decent size incoming buffer.

	if (-1 == setsockopt(tmp, SOL_SOCKET, SO_SNDBUF, &szSnd, sizeof(szSnd)))
	    syslog(LOG_WARNING, "couldn't set send buf size for TCP client socket -- %m");
	if (-1 == setsockopt(tmp, SOL_SOCKET, SO_RCVBUF, &szRcv, sizeof(szRcv)))
	    syslog(LOG_WARNING, "couldn't set rcv buf size for TCP client socket -- %m");

	int v = 1;
	if (-1 == setsockopt(tmp, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)))
	    syslog(LOG_WARNING, "couldn't set SO_REUSEADDR for TCP client socket -- %m");
#ifdef SO_REUSEPORT
	if (-1 == setsockopt(tmp, SOL_SOCKET, SO_REUSEPORT, &v, sizeof(v)))
	    syslog(LOG_WARNING, "couldn't set SO_REUSEPORT for TCP client socket -- %m");
#endif

	if (-1 != fcntl(tmp, F_SETFL, O_NONBLOCK)) {

	    // Bind the socket to the requested port and address.

	    sockaddr_in in;

#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
	    in.sin_len = sizeof(in);
#endif
	    in.sin_family = AF_INET;
	    in.sin_port = htons(port);
	    in.sin_addr.s_addr = htonl(addr);

	    if (-1 != ::bind(tmp, reinterpret_cast<sockaddr*>(&in), sizeof(in))) {
		listen(tmp, 128);
		return tmp;
	    } else
		syslog(LOG_ERR, "couldn't bind() to tcp port %d -- %m", port);
	} else
	    syslog(LOG_ERR, "couldn't set tcp socket to non-blocking -- %m");

	close(tmp);
    } else
	syslog(LOG_ERR, "couldn't create socket -- %m");

    return -1;
}

void dumpOutgoingAcnetPackets(bool status)
{
    dumpOutgoing = status;
    syslog(LOG_INFO, "Dumping outgoing ACNET packets: %s", status ? "ON" : "OFF");
}

// Initializes the network portion of the application.

bool networkInit(uint16_t port)
{
    return -1 != (sNetwork = allocSocket(INADDR_ANY, port, 128 * 1024, 128 * 1024));
}

// Releases resources used by the network.

void networkTerm()
{
    // Close the network socket.

    if (-1 != sNetwork) {
	close(sNetwork);
	sNetwork = -1;
    }

    // Empty out the outgoing queue.

    while (!outgoing.empty())
	delete outgoing.pop();
}

// Reads the next packet from the given socket.

ssize_t readNextPacket(void* const buffer, size_t const len, sockaddr_in& in)
{
    socklen_t in_len = sizeof(sockaddr_in);
#ifdef NO_SWAP
    ssize_t const res = recvfrom(sNetwork, buffer, len, 0, (sockaddr*) &in, &in_len);

    if (res < 0 && errno != EAGAIN)
	syslog(LOG_WARNING, "couldn't read from network socket -- %m");
#else
    ssize_t const res = recvfrom(sNetwork, buffer, len, 0, (sockaddr*) &in, &in_len);

    if (res > 0) {
	size_t const total = (size_t) res < len ? (size_t) res : len;

	// We first need to byte-swap the entire packet. One day, in the glorious future, this stupid, historical artifact
	// will be removed from the ACNET protocol. But not today...

	for (size_t ii = 0; ii < total / 2; ii++)
	    ((uint16_t *) buffer)[ii] = swap(((uint16_t *) buffer)[ii]);

    } else if (errno != EAGAIN)
	syslog(LOG_WARNING, "couldn't read from network socket -- %m");
#endif
    return res;
}

static char const* dumpBuffer(void const* const buf, size_t const len)
{
    static char out[256];
    size_t ii;

    for (ii = 0; ii < len && ii < sizeof(out) / 3; ++ii) {
        static char const hex[] = "0123456789abcdef";

        out[ii * 3] = ' ';
        out[ii * 3 + 1] = hex[(((uint8_t const*) buf)[ii] >> 4) & 0xf];
        out[ii * 3 + 2] = hex[((uint8_t const*) buf)[ii] & 0xf];
    }
    out[ii * 3] = '\0';
    return out;
}

void dumpPacket(const char* pktType, AcnetHeader const& hdr, void const* d,
		size_t msgLen)
{
    static char const* const txt[] = {
	"USM", "REQ", "RPY", "**3", "**4", "**5", "**6", "**7"
    };
    uint16_t const flags = hdr.flags();

    syslog(LOG_NOTICE, "%s Packet : flags (%s%s%s%s), type %s, status 0x%04x, "
	   "svr (0x%04x), clnt (0x%04x), svrTskNm '%s' (0x%08x), "
	   "cTskId 0x%02x, msgId 0x%04x, msgLen %d -- %s", pktType,
	   ((flags & ACNET_FLG_CHK) ? " CHK " : ""),
	   ((flags & ACNET_FLG_CAN) ? " CAN " : ""),
	   ((flags & ACNET_FLG_NBW) ? " NBW " : ""),
	   ((flags & ACNET_FLG_MLT) ? " MLT " : ""),
	   txt[(flags & ACNET_FLG_TYPE) >> 1], hdr.status().raw(),
	   hdr.server().raw(), hdr.client().raw(),
	   hdr.svrTaskName().str(), hdr.svrTaskName().raw(),
	   hdr.clntTaskId().raw(), hdr.msgId().raw(), (int) msgLen,
	   dumpBuffer(d, msgLen - sizeof(AcnetHeader)));
}

int sendDataToNetwork(AcnetHeader const& hdr, void const* d, size_t n)
{
    trunknode_t const dst = ((hdr.flags() & ACNET_FLG_TYPE) == ACNET_FLG_RPY) ?
	    hdr.client() : hdr.server();

    if (dumpOutgoing)
	dumpPacket("Outgoing", hdr, d, sizeof(AcnetHeader) + n);

    DataOut* ptr = partialBuffer(dst);

    // We need to allocate a new packet under two conditions: if there isn't a partial buffer associated with the target
    // node or if we can't add our data block to the current buffer.

    if (!ptr || !ptr->addData(hdr, d, n)) {
	try {
	    ptr = allocPacket(dst);

	    // Now we try to add our data again. This should never fail because the packets are sized to support our
	    // largest datagram and we just allocated an empty packet. If it fails, complain loudly to the log!

	    if (!ptr->addData(hdr, d, n)) {
		syslog(LOG_ERR, "sendDataToNetwork() couldn't add data to a packet -- packet has been lost");
		return 0;
	    }
	}
	catch (...) {
	    return 0;
	}
    }
    return 1;
}

// Sends all pending packets to the network interface. If the queue becomes empty, this function returns true. If there is
// still work to be done, it returns false.

bool sendPendingPackets()
{
    while (!outgoing.empty()) {
	DataOut* const ptr = outgoing.peek();
	trunknode_t const target = ptr->getTarget();

	// Send the packet's data to the socket.

	sockaddr const* const addr = (sockaddr const*) getAddr(target);

	if (addr) {
	    ssize_t const len = sendto(sNetwork, ptr->getPacketData(), ptr->getPacketSize(), 0, addr, sizeof(sockaddr_in));

	    if (-1 == len) {

		// If there isn't room in the outgoing buffer, we return false so the caller stops calling us for this poll()
		// loop.

		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EMSGSIZE)
		    return false;
		else {
		    ipaddr_t ip = ipaddr_t(ntohl(((sockaddr_in *) addr)->sin_addr.s_addr));
		    syslog(LOG_WARNING, "couldn't send packet to socket -- %m (%s)", ip.str().c_str());
		}
	    }
	} else if (dumpOutgoing) {
	    AcnetHeader const* const hdr = reinterpret_cast<AcnetHeader const*>(ptr->getPacketData());

	    syslog(LOG_WARNING, "couldn't look up node 0x%02x%02x for sending packet -- discarding (Info => svr: 0x%04x, "
		   "cln: 0x%04x, tsk: 0x%08x, msgId: 0x%04x)", target.trunk().raw(), target.node().raw(), hdr->server().raw(),
		   hdr->client().raw(), hdr->svrTaskName().raw(), hdr->msgId().raw());
	}

	// If this packet is associated with a target as being partially filled, we disassociate it. Not enough outgoing
	// packets filled the packet this time.

	if (partialBuffer(target) == ptr)
	    setPartialBuffer(target, 0);

	delete outgoing.pop();
    }
    return true;
}

// This function is used to return errors to remote requestors. It turns out that bad replies sent to us can be responded to
// with cancels. USMs can be silently dropped, since the remote application isn't looking for a reply.

void sendErrorToNetwork(AcnetHeader const& hdr, status_t err)
{
    assert((hdr.flags() & ACNET_FLG_TYPE) == ACNET_FLG_REQ);

    AcnetHeader const xmt(ACNET_FLG_RPY, err, hdr.server(), hdr.client(),
			  hdr.svrTaskName(), hdr.clntTaskId(),
			  hdr.msgId(), sizeof(AcnetHeader));

    (void) sendDataToNetwork(xmt, 0, 0);
}

// This function queues up a USM to be sent on the network socket.

void sendUsmToNetwork(trunknode_t tgtNode, taskhandle_t tgtTask, nodename_t fromNodeName,
			taskid_t client, uint8_t const* data, size_t len)
{
    trunknode_t fromNode;

    nameLookup(fromNodeName, fromNode);

    if (getAddr(tgtNode) != 0) {
	AcnetHeader const ah(ACNET_FLG_USM, ACNET_SUCCESS, tgtNode, fromNode, tgtTask, client, reqid_t(), MSG_LENGTH(len) + sizeof(AcnetHeader));

	(void) sendDataToNetwork(ah, data, len);
    } else if (dumpOutgoing)
	syslog(LOG_WARNING, "IP table has no entry for node 0x%02x%02x -- cancelling USM transmission",
	       tgtNode.trunk().raw(), tgtNode.node().raw());
}

// Local Variables:
// mode:c++
// fill-column:125
// End:
