#include "server.h"
#ifndef NO_REPORT
#include <iomanip>
#endif
#include <sys/socket.h>
#include <sys/param.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <cstring>

// Local types

#define	ILLEGAL_NODE	nodename_t(0xfffffffful)

// Local prototypes

static void eraseNode(trunknode_t);
static void insertNode(trunknode_t, nodename_t, uint32_t);
static IpInfo* returnTrunk(trunk_t);

// Local data

static IpInfo* addrMap[256] = { 0 };
static trunknode_t myNode_;
static uint32_t myIp_ = 0;
static nodename_t myHostName_;
static time_t lastNodeTableDownloadTime_ = 0;

IpInfo::IpInfo() : name_(ILLEGAL_NODE), partial(0)
{
    in.sin_family = AF_INET;
    in.sin_port = htons(0);
    in.sin_addr.s_addr = htonl(0);
#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
    in.sin_len = sizeof(in);
#endif
}

IpInfo::IpInfo(nodename_t name, uint32_t a, uint16_t port) :
    name_(name), partial(0)
{
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    in.sin_addr.s_addr = htonl(a);
#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
    in.sin_len = sizeof(in);
#endif
}

void generateKillerMessages()
{
    assert(myIp_ != 0);
    uint32_t const addr = htonl(myIp_);

    for (size_t trunk = 0; trunk < sizeof(addrMap) / sizeof(*addrMap); ++trunk)
	if (addrMap[trunk])
	    for (size_t node = 0; node < 256; ++node) {
		IpInfo const& data = addrMap[trunk][node];

		if (data.name() != ILLEGAL_NODE &&
		    data.addr()->sin_addr.s_addr == addr)
		    sendKillerMessage(trunknode_t(trunk, node));
	    }
}

bool addrLookup(uint32_t a, trunknode_t& tn)
{
    for (size_t trunk = 0; trunk < 256; ++trunk) {
	IpInfo const* const ptr = addrMap[trunk];

	if (ptr)
	    for (size_t node = 0; node < 256; ++node) {
		IpInfo const* const record = ptr + node;

		if (record->name() != ILLEGAL_NODE && htonl(a) == record->addr()->sin_addr.s_addr) {
		    tn = trunknode_t(trunk, node);
		    return true;
		}
	    }
    }
    return false;
}

// Clears out the entry for the given trunk and node.

static void eraseNode(trunknode_t tn)
{
    if (addrMap[tn.trunk()])
	addrMap[tn.trunk()][tn.node()].update(ILLEGAL_NODE, 0);
}

// Returns the IpInfo structure associated with the given trunk and node.

IpInfo* findNodeInfo(trunknode_t tn)
{
    IpInfo* const ptr = addrMap[tn.trunk()];

    if (ptr) {
	IpInfo* const data = ptr + tn.node();

	if (data->name() != ILLEGAL_NODE)
	    return data;
    }
    return 0;
}

// Writes a report of the IP table to the specified stream.

#ifndef NO_REPORT
void generateIpReport(std::ostream& os)
{
    os << "\t\t<div class=\"section\">\n"
	"\t\t<h1>IP Table Report</h1>\n" << std::setw(0) << std::hex << std::setfill(' ');

    time_t t = lastNodeTableDownloadTime();
    if (t)
	os << "\t\t<p>Last node table download: " << ctime(&t) << "<p>";
    else
	os << "<p>Waiting for node table download<p>";

    os << "\t\t<table = width=\"80%\">\n"
	"\t\t\t<colgroup>\n"
	"\t\t\t\t<col class=\"label\"/>\n"
	"\t\t\t\t<col/>\n"
	"\t\t\t</colgroup>\n"
	"\t\t\t<thead>\n"
	"\t\t\t\t<tr><td>TRUNK</td><td>NODE</td><td>IP Address</td><td>NAME</td></tr>\n"
	"\t\t\t</thead>\n"
	"\t\t\t<tbody>\n";

    bool even = false;

    for (size_t trunk = 0; trunk < 256; ++trunk) {
	IpInfo const* const base = addrMap[trunk];

	if (base)
	    for (size_t node = 0; node < 256; ++node) {
		IpInfo const* const record = base + node;

		if (record->name() != ILLEGAL_NODE) {
		    uint32_t const ip = ntohl(record->addr()->sin_addr.s_addr);

		    os << "\t\t\t<tr" << (even ? " class=\"even\"" : "") << "><td>" << std::hex << (unsigned) trunk <<
			"</td><td>" << (unsigned) node << "</td><td>" << std::dec << (ip >> 24) << '.' << ((ip >> 16) & 0xff) <<
			'.' << ((ip >> 8) & 0xff) << '.' << (ip & 0xff) << "</td><td>" << rtoa(record->name().raw()) << "</td></tr>\n";
		    even = !even;
		}
	    }
    }
    os << "\t\t\t</tbody>\n\t\t</table>\n\t\t</div>\n";
}
#endif

// Get address for specified trunk/node.

sockaddr_in const *getAddr(trunknode_t tn)
{
    IpInfo const* const ii = findNodeInfo(tn);

    return ii ? ii->addr() : 0;
}

uint32_t getIpAddr(char const host[])
{
    hostent const* const he = gethostbyname(host);

    return he ? ntohl(*(uint32_t*) he->h_addr_list[0]) : 0;
}

time_t lastNodeTableDownloadTime()
{
    return lastNodeTableDownloadTime_;
}

uint32_t myIp()
{
    return myIp_;
}

trunknode_t myNode()
{
    return myNode_;
}

void setMyHostName(nodename_t newHostName)
{
    myHostName_ = newHostName;
}

nodename_t myHostName()
{
    return myHostName_;
}

// Returns the partial buffer associated with a node. If the node isn't in the map or the node doesn't have a partial buffer,
// then a NULL pointer is returned.

DataOut* partialBuffer(trunknode_t tn)
{
    IpInfo const* const ii = findNodeInfo(tn);

    return ii ? ii->partialBuffer() : 0;
}

// Inserts a node into the node table.

static void insertNode(trunknode_t tn, nodename_t name, uint32_t a)
{
    IpInfo* const ptr = returnTrunk(tn.trunk());

    assert(ptr);

    ptr[tn.node()].update(name, a);
}

bool isMulticastHandle(taskhandle_t th)
{
    uint32_t a;

    if (nameLookup(nodename_t(th), a))
	return IN_MULTICAST(a);

    return false;
}

bool isMulticastNode(trunknode_t const tn)
{
    sockaddr_in const* const addr = getAddr(tn);

    return addr ? IN_MULTICAST(ntohl(addr->sin_addr.s_addr)) : false;
}

bool isThisMachine(trunknode_t const tn)
{
    IpInfo const* const ptr = findNodeInfo(tn);

    return ptr && myIp_ == ntohl(ptr->addr()->sin_addr.s_addr);
}

// Lookup the trunknode_t for a given nodename_t (slowly)

bool nameLookup(nodename_t name, trunknode_t& tn)
{
    for (size_t trunk = 0; trunk < 256; ++trunk) {
	IpInfo const* const ptr = addrMap[trunk];

	if (ptr)
	    for (size_t node = 0; node < 256; ++node) {
		IpInfo const* const record = ptr + node;

		if (record->name() != ILLEGAL_NODE && record->matches(name)) {
		    tn = trunknode_t(trunk, node);
		    return true;
		}
	    }
    }
    return false;
}

bool nameLookup(nodename_t name, uint32_t& a)
{
    for (size_t trunk = 0; trunk < 256; ++trunk) {
	IpInfo const* const ptr = addrMap[trunk];

	if (ptr)
	    for (size_t node = 0; node < 256; ++node) {
		IpInfo const* const record = ptr + node;

		if (record->name() != ILLEGAL_NODE && record->matches(name)) {
		    a = ntohl(record->addr()->sin_addr.s_addr);
		    return true;
		}
	    }
    }
    return false;
}

bool nodeLookup(trunknode_t tn, nodename_t& name)
{
    IpInfo const* const ii = findNodeInfo(tn);

    if (ii) {
	name = ii->name();
	return true;
    }
    return false;
}

// Returns a pointer to an array of IpInfo structures associated with the specified trunk.

static IpInfo* returnTrunk(trunk_t t)
{
    IpInfo*& location = addrMap[t];

    if (!location)
	location = new IpInfo[256];
    return location;
}

void setLastNodeTableDownloadTime()
{
    lastNodeTableDownloadTime_ = now().tv_sec;
}

// This function is called when acnetd starts up. It does a DNS lookup of its own name to get its IP address. This way, when
// the IP table gets populated, we can determine our own trunk and node.

void setMyIp()
{
    char name[MAXHOSTNAMELEN];

    // Fill in our buffer with the current machine's network name.

    (void) gethostname(name, sizeof(name));

    // Do a DNS lookup of the name. If we succeed, we remember the IP address. Otherwise report an error indicating a
    // reduction in service.

    if (0 == (myIp_ = getIpAddr(name)))
	syslog(LOG_WARNING, "DNS failure, '%s' -- we won't be able to recognize local traffic", hstrerror(h_errno));

    // If the hostName wasn't set on the command line then use the os hostname

    if (myHostName_.isBlank()) {

	// Only use the host part of a fully-qualified hostname

	char *p = strchr(name, '.');
	if (p)
	    *p = 0;

	// Save the rad50 equivalent of our host name

	myHostName_ = nodename_t(ator(name));
    }

    // Add generic multicast to trunk/node table

    insertNode(ACNET_MULTICAST, nodename_t(ator("MCAST")), octetsToIp(239, 128, 4, 1));
    joinMulticastGroup(sClient, ipaddr_t(octetsToIp(239, 128, 4, 1)));
}

// Associates a partially filled buffer with an ACNET node.

void setPartialBuffer(trunknode_t tn, DataOut* ptr)
{
    IpInfo* const ii = findNodeInfo(tn);

    if (ii)
	ii->setPartialBuffer(ptr);
}

bool trunkExists(trunk_t t)
{
    return addrMap[t];
}

// Update trunk/node lookup to map (used for internal node table entries)

void updateAddr(trunknode_t tn, nodename_t newName, uint32_t newAddr)
{
    // Don't let an application add an entry for the LOCAL address.

    if (tn.isBlank()) {
	syslog(LOG_WARNING, "an attempt was made to set invalid address 0x%02x%02x", tn.trunk(), tn.node());
	return;
    }

    // If the IP address matches ours, we might have to update our primary trunk and node.

    if (newAddr == myIp_) {

	// If the node name is -1, then we're adding an entry that will get overwritten. Since we know the IP addresses
	// match, we'll update the name to our hostname.

	if (newName == ILLEGAL_NODE)
	    newName = myHostName_;

	// If we don't have our node yet, use the first one that matches the ip addr without
	// checking the name since the hostname may not be the same as the ACNET name.

	if (myNode_.isBlank())
	    myNode_ = tn;

	// If the hostnames match, then we're updating our primary node. In this case, we store the trunk and node for quick
	// look-up. This will be the default trunk and node for connections.

	if (newName == myHostName_) {
	    if (!myNode_.isBlank() && myNode_ != tn)
		syslog(LOG_WARNING, "trunk and node, for this machine, was changed from (0x%02x%02x) to (0x%02x%02x)",
		       myNode_.trunk(), myNode_.node(), tn.trunk(), tn.node());
	    myNode_ = tn;
	}
    } else if (newName == ILLEGAL_NODE)
	newName = nodename_t(ator("%%%%%%"));

    if (newName.isBlank() && !newAddr)
	eraseNode(tn);
    else {
	IpInfo* const ii = findNodeInfo(tn);

	// If the node already exists in our table, we simply update the fields. If it doesn't exist, we need to insert a new
	// node into the map.

	if (ii) {
	    if (htonl(newAddr) != ii->addr()->sin_addr.s_addr) {
		cancelReqToNode(tn);
		endRpyToNode(tn);
	    }

	    // Update the fields.

	    ii->update(newName, newAddr);
	} else
	    insertNode(tn, newName, newAddr);
    }
}

bool validFromAddress(char const proto[], trunknode_t const ctn, uint32_t const in, uint32_t const ip)
{
    if (ip == in)
	return true;
    else {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Dropping %s from %d.%d.%d.%d == %d.%d.%d.%d? -- client masquerading as 0x%02x%02x", proto,
		   in >> 24, (uint8_t) (in >> 16), (uint8_t) (in >> 8), (uint8_t) in,
		   ip >> 24, (uint8_t) (ip >> 16), (uint8_t) (ip >> 8), (uint8_t) ip,
		   ctn.trunk(), ctn.node());
	return false;
    }
}

bool validToAddress(char const proto[], trunknode_t const src, trunknode_t const dst)
{
    if (isThisMachine(dst))
	return true;
    else {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Dropping %s -- (from 0x%02x%02x) dst node 0x%02x%02x is not this machine)", proto, src.trunk(), src.node(), dst.trunk(), dst.node());
	return false;
    }
}

// Local Variables:
// mode:c++
// fill-column:125
// End:
