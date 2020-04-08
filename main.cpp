#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <errno.h>
#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include "server.h"
#include "exttask.h"
#if THIS_TARGET == NetBSD_Target
#include <sys/resource.h>
#include <util.h>
#endif

typedef std::map<nodename_t, TaskPool *> TaskPoolMap;

// Local types...

// Local prototypes...

static DeltaTime errorConditions();
static ipaddr_t ipAddress(trunknode_t&, ipaddr_t, bool);
static bool getResources();
static bool isPacketSizeValid(size_t, uint16_t, size_t, ipaddr_t);
static DeltaTime normalConditions();
static void releaseResources();
static void sigHup(int);
static void sigInt(int);
static DeltaTime waitingForNodeTable();

// Global data...

bool dumpIncoming = false;
AbsTime statTimeBase = now();
int sClient = -1;
uint16_t acnetPort = ACNET_PORT;

// Local variables...

static TaskPoolMap taskPoolMap;
static int sClientTcp = -1;
static nodename_t tcpNodeName;
static bool defaultNodeFallback = true;

#ifndef NO_REPORT
static bool sendReport = false;
#endif
bool termSignal = false;
static bool termApp = false;
static DeltaTime (*nodeTableConstraints)() = waitingForNodeTable;
static AbsTime currTime;

struct CmdLineArgs {
    trunknode_t myNode;
    bool standAlone;
    bool alternate;
    bool tcpClients;
    uint16_t altPort;
    std::set<taskhandle_t> taskReject;

    CmdLineArgs() :
	standAlone(false), alternate(false), tcpClients(false)
    {
    }

    bool proc(int argc, char **argv)
    {
	for (int ii = 1; ii < argc; ++ii) {
	    char const* curPtr = argv[ii];

	    if (*curPtr == '-') {
		bool done = false;

		++curPtr;

		while (!done)
		    switch (*curPtr++) {
		     case '\0':
			done = true;
			break;

		     case 'n':
			if (!*curPtr) {
			    if (ii < argc - 1 && argv[ii + 1][0] != '-')
				curPtr = argv[++ii];
			    else {
				printf("missing trunk/node argument to '-n' option\n\n");
				return false;
			    }
			}
			if (!getTrunkNode(&curPtr, myNode, '\0')) {
			    printf("Bad trunk/node value\n\n");
			    return false;
			}
			break;

		     case 'H':
			if (!*curPtr) {
			    if (ii < argc - 1 && argv[ii + 1][0] != '-')
				curPtr = argv[++ii];
			    else {
				printf("missing name argument to '-H' option\n\n");
				return false;
			    }
			}
			setMyHostName(nodename_t(curPtr));
			done = true;
			break;

		     case 's':
			standAlone = true;
			break;

		     case 'a':
			alternate = true;
			if (!*curPtr) {
			    if (ii < argc - 1 && isdigit(argv[ii + 1][0]))
				curPtr = argv[++ii];
			    else {
				printf("missing port argument to '-a' option\n\n");
				return false;
			    }
			}
			if (!getAltPort(&curPtr)) {
			    printf("Bad port value\n");
			    return false;
			}
			done = true;
			break;

		     case 't':
			tcpClients = true;
			if (!*curPtr) {
			    if (ii < argc - 1 && argv[ii + 1][0] != '-')
				curPtr = argv[++ii];
			    else {
				printf("missing name argument to '-t' option\n\n");
				return false;
			    }
			}
			tcpNodeName = nodename_t(curPtr);
			done = true;
			break;

		     case 'r':
			if (!*curPtr) {
			    if (ii < argc - 1 && argv[ii + 1][0] != '-')
				curPtr = argv[++ii];
			    else {
				printf("missing task name list to '-r' option\n\n");
				return false;
			    }
			}
			getTaskRejectList(curPtr);
			done = true;
			break;

		     case 'f':
			defaultNodeFallback = false;
			syslog(LOG_NOTICE, "default node fallback is off");
			break;

		     case 'h':
		     case '?':
			return false;

		     default:
			printf("Unknown option '-%c'\n\n", curPtr[-1]);
			return false;
		    }
	    } else {
		printf("unknown argument -- '%s'\n\n", curPtr);
		return false;
	    }
	}
	return true;
    }

    void showUsage()
    {
	printf("Usage: acnetd [-h] [-s -nTRUNKNODE]\n"
	       "   -h            display help\n"
	       "   -s            stand-alone mode -- don't try to download\n"
	       "                 ACNET node tables\n"
	       "   -t name       allow TCP client connections on host name\n"
	       "   -r list       comma seperated list of task handles to reject on TCP connections\n"
	       "   -H name       sets the ACNET host name of this node\n"
	       "   -n TRUNKNODE  sets the current trunk and node to the\n"
	       "                 specified four hex digits\n"
	       "   -f            turn off default node fallback\n"
	       "   -a port       use alternate port\n");
    }

    void getTaskRejectList(std::string s)
    {
	std::string name;
	std::istringstream is(s);

	while (getline(is, s, ',')) {
	    taskReject.insert(taskhandle_t(s));
	    syslog(LOG_NOTICE, "rejecting requests to task '%s'", s.c_str());
	}
    }

    bool getAltPort(char const** const buf)
    {
	unsigned long v = strtol(*buf, NULL, 0);

	if (v != 0 && v != ACNET_CLIENT_PORT && v <= 65535) {
	    altPort = (uint16_t) v;
	    return true;
	}

	return false;
    }

    bool getTrunkNode(char const** const buf, trunknode_t& node, char endCh)
    {
	uint16_t _node = 0;

	while (true) {
	    char c = tolower(**buf);

	    if (isdigit(c))
		_node = _node * 16 + (c - '0');
	    else if (c >= 'a' && c <= 'f')
		_node = _node * 16 + (10 + (c - 'a'));
	    else if (c == endCh) {
		node = trunknode_t(_node);
		return true;
	    } else
		return false;

	    (*buf)++;
	}
    }

    bool getTrunkNodeAddrPair(char const* buf, trunknode_t& node,
			      uint32_t& addr)
    {
	if (getTrunkNode(&buf, node, ':') && !node.isBlank())
	    if (*buf == ':')
		if ((addr = ipAddr(++buf)) != 0)
		    return true;
	return false;
    }
} cmdLineArgs;

bool rejectTask(taskhandle_t const task)
{
    return cmdLineArgs.taskReject.find(task) != cmdLineArgs.taskReject.end();
}

void dumpIncomingAcnetPackets(bool const status)
{
    dumpIncoming = status;
    syslog(LOG_NOTICE, "Dumping incoming ACNET packets: %s", status ? "ON" : "OFF");
}

static void getCurrentTime()
{
    struct timeval now;

    gettimeofday(&now, 0);
    currTime = AbsTime(now.tv_sec * 1000LL + now.tv_usec / 1000LL);
}

AbsTime now()
{
    if (!currTime)
	getCurrentTime();
    return currTime;
}

// This function tries to get all the resources needed for the ACNET
// protocol.  This means, of course, the datagram socket sitting at port
// ACNET_PORT or the user specified port.  But it also includes the
// local datagram socket used to communicate with local clients.

static bool getResources()
{
    // Initialize the random number generator. We use random numbers to
    // select a bank of IDs for requests and for replies.

    srandom(time(0));

    // Open the network socket(s).

    if (networkInit(acnetPort)) {
#if THIS_TARGET == Darwin_Target
#define PLATFORM_INADDR	INADDR_ANY
#else
#define	PLATFORM_INADDR	INADDR_LOOPBACK
#endif
	if (-1 != (sClient = allocSocket(PLATFORM_INADDR, ACNET_CLIENT_PORT, 128 * 1024, 128 * 1024))) {
	    setMyIp();

	    if (cmdLineArgs.tcpClients) {
		if (-1 != (sClientTcp = allocClientTcpSocket(INADDR_ANY, ACNET_CLIENT_PORT, 128 * 1024, 128 * 1024)))
		    syslog(LOG_NOTICE, "TCP client interface enabled");
		else
		    syslog(LOG_ERR, "unable to allocate client TCP socket -- %m");
	    }

	    return true;
	}
	networkTerm();
    }
    return false;
}

void cancelReqToNode(trunknode_t const tn)
{
    auto ii = taskPoolMap.begin();

    while (ii != taskPoolMap.end())
	(*ii++).second->reqPool.cancelReqToNode(tn);
}

void endRpyToNode(trunknode_t const tn)
{
    auto ii = taskPoolMap.begin();

    while (ii != taskPoolMap.end())
	(*ii++).second->rpyPool.endRpyToNode(tn);
}

static void handleAcnetUsm(TaskPool *taskPool, AcnetHeader const& hdr)
{
    // We have a regular USM. We look up the destination task and, if it
    // is listening, deliver the packet to it.

    auto ii = taskPool->tasks(hdr.svrTaskName());

    while (ii.first != ii.second) {
	TaskInfo * const task = ii.first->second;

	if (task->acceptsUsm())
	    if (task->sendDataToClient(&hdr))
		++task->stats.usmRcv;

	++ii.first;
    }

    ++taskPool->stats.usmRcv;
}

static void handleAcnetCancel(TaskPool *taskPool, AcnetHeader& hdr)
{
    // We have a CANCEL packet. Look for the corresponding Reply structure.

    RpyInfo const* rpy;

    while (0 != (rpy = taskPool->rpyPool.rpyInfo(hdr.client(), hdr.msgId()))) {

	// Since we just found this msg id then the rpy info should have the
	// same value. If not, then it's a bug in acnetd

	assert(hdr.msgId() == rpy->reqId());

	// Check if the header matches our knowledge of the request

	if (hdr.svrTaskName() != rpy->taskName()) {
	    char buf1[16], buf2[16];

	    syslog(LOG_ERR, "mismatched CANCEL task name (%s != %s) "
		   "received from node 0x%04x",
		   hdr.svrTaskName().str(buf1),
		   rpy->taskName().str(buf2),
		   hdr.client().raw());
	    return;
	}

	if (hdr.client() != rpy->remNode()) {
	    syslog(LOG_ERR, "mismatched CANCEL client/remote nodes "
		   "(0x%04x != 0x%04x) received from node 0x%04x",
		   hdr.client().raw(), rpy->remNode().raw(),
		   hdr.client().raw());
	    return;
	}

	if (hdr.clntTaskId() != rpy->taskId()) {
	    syslog(LOG_ERR, "mismatched CANCEL task id (%d != %d) "
		   "received from node 0x%04x",
		   hdr.clntTaskId().raw(), rpy->taskId().raw(), hdr.client().raw());
	    return;
	}

	assert(rpy->task().acceptsRequests());

#ifdef DEBUG
	syslog(LOG_NOTICE, "CANCEL REQUEST: id = 0x%04x", rpy->reqId().raw());
#endif

	// NOTICE: Our data passing protocol between acnetd and local
	// clients dictates that all packets passed through the data
	// socket must be an ACNET packet.  The ACNET packets don't have
	// a field for Reply IDs. We assume all CANCELs have a 0 ACNET
	// status, so we stuff the Reply ID into the status field. The
	// client library knows to pull the Reply ID from the status when
	// cleaning up its tables, but report 0 to the application.

	TaskInfo& task = rpy->task();

	if (!rpy->beenAcked())
	    task.decrementPendingRequests();

	hdr.setStatus(rpy->id());
	taskPool->rpyPool.endRpyId(rpy->id());

	if (task.sendDataToClient(&hdr)) {
	    ++task.stats.usmRcv;
	    ++taskPool->stats.usmRcv;
	} else
	    taskPool->removeTask(&task);
    }
}

static void handleAcnetRequest(TaskPool *taskPool, AcnetHeader& hdr)
{
    sockaddr_in const* const in = getAddr(hdr.server());
    bool const isMulticast = in && IN_MULTICAST(htonl(in->sin_addr.s_addr));
    auto ii = taskPool->tasks(hdr.svrTaskName());

    // If the request is a multicast request, then we don't want to send back
    // a ACNET_NOTASK because other multicast recipients may have the
    // properly named task and will respond. Our ACNET_NOTASK would
    // prematurely close the request.
    //
    // If 'sendError' is true, then we don't have any listeners. We look to
    // see if the target node is a multicast node. If so, 'sendError' gets
    // cleared and we won't send the ACNET_NOTASK.

    if (ii.first == ii.second) {
	if (!isMulticast)
	    sendErrorToNetwork(hdr, ACNET_NOTASK);
	return;
    }

    TaskInfo *deadTask = 0;
    bool sendError = true;

    ++taskPool->stats.reqRcv;

    while (ii.first != ii.second) {
	status_t result = ACNET_SUCCESS;
	TaskInfo * const task = ii.first->second;

	++(ii.first);

	// We test to see if the task is still alive. If a client gets
	// disconnected abruptly, acnetd will still think it's alive until
	// any communications is attempted. If this call fails, we remove the
	// task, which will clean up all the resources bound to the task.

	if (task->stillAlive()) {
	    sendError = false;

	    // Even if the task is alive, it may not be a listening task.

	    if (task->acceptsRequests()) {

		// Check to see if the client has been slow in handling
		// requests. If it has been too slow, send an error to the
		// remote client.

#ifdef DEBUG
		syslog(LOG_INFO, "NEW REQUEST: id = 0x%04x",
		       hdr.msgId().raw());
#endif
		if (task->testPendingRequestsAndIncrement()) {
		    try {
			// At this point, we believe we have a valid ACNET
			// request. Allocate a Reply structure so the client
			// can reply.

			RpyInfo const* const rpy = taskPool->rpyPool.alloc(task, hdr.msgId(), hdr.clntTaskId(),
							       hdr.svrTaskName(), hdr.server(),
							       hdr.client(), hdr.flags());

			// Send the packet to the client. If the
			// communications fails, we deallocate the reply.
			// NOTICE: See the NOTICE section above (in handling
			// CANCELs) for the reason we stuff the reply ID in
			// the status field.

			hdr.setStatus(rpy->id());
			if (task->sendDataToClient(&hdr)) {
			    ++task->stats.reqRcv;
			    continue;
			}
			result = ACNET_BUSY;
			taskPool->rpyPool.endRpyId(rpy->id(), ACNET_DISCONNECTED);
		    }
		    catch (...) {
			result = ACNET_NOREMMEM;
		    }
		    task->decrementPendingRequests();
		} else
		    result = ACNET_FUL;

	    } else
		result = ACNET_NCR;

	    if (!isMulticast)
		sendErrorToNetwork(hdr, result);
	} else

	    // Since the associated client isn't responding, we'll assume
	    // it's dead and free up the resources assigned to it. This only
	    // allows us to remote one dead task per call to this function,
	    // but that's alright -- others will get cleaned up in future
	    // calls to the function.

	    deadTask = task;
    }

    // If there are no tasks with the requested handle, return an error to
    // the sender.

    if (sendError && !isMulticast)
	sendErrorToNetwork(hdr, ACNET_NOTASK);

    // Did we find a dead task? If so, free up its associated resources.

    if (deadTask) {
	taskPool->removeTask(deadTask);
    }
}

// The following function has some sanity checks to determine whether a
// client should receive the packet and whether the ACNET protocol was
// violated, requiring a CANCEL to be sent. This all became more complicated
// when we added ACNET_PEND statuses which keep the connection open, but
// aren't sent up to the client.
//
// I encoded the conditions in a Karnaugh map to try to document and simplify
// the boolean expressions. This function's sanity checks are based upon the
// following tables. There are 4 "inputs" to the logic:
//
//     A <- The Request expects multiple replies
//     B <- The Request was multicasted
//     C <- The incoming packet is EMR (End of Multiple Reply)
//     D <- The incoming packet has ACNET_PEND status
//
// The first table calculates whether the request should be cleared up in our
// local tables.
//
//        !A!B   !A B    A B    A!B
//   !C!D   1      1      0      0
//   !C D   0      0      0      0
//    C D   0      0      0      0
//    C!D   1      1      0      1
//
//   Which yields     A!BC!D + !A!D
//                    (A!BC + !A)!D
//                    (!BC + !A)!D
//
// The second table calculates whether a CANCEL needs to be sent to the
// remote node. This is also the condition in which we need to fix the header
// to the client because we asked for a single reply and the client sent one
// of multiple replies.
//
//        !A!B   !A B    A B    A!B
//   !C!D   1      1      0      0
//   !C D   0      0      0      0
//    C D   0      0      0      0
//    C!D   0      1      0      0
//
//   Which yields     !A!C!D + !ABC!D
//                    !A!D(!C + BC)
//
// The third table determines whether we need to adjust the header because
// we sent a multicasted request for multiple request and this response has
// the EMR set. The only way to stop a multicast request for multiple replies
// is to multicast a CANCEL.
//
//        !A!B   !A B    A B    A!B
//   !C!D   0      0      0      0
//   !C D   0      0      0      0
//    C D   0      0      1      0
//    C!D   0      0      1      0
//
//   Which yields     ABC
//
// The fourth table calculates whether the packet should be sent to the client.
//
//        !A!B   !A B    A B    A!B
//   !C!D   1      1      1      1
//   !C D   0      0      0      0
//    C D   0      0      0      0
//    C!D   1      1      1      1
//
//   Which yields     !D

static void handleAcnetReply(TaskPool *taskPool, AcnetHeader& hdr)
{
    reqid_t const msgId = hdr.msgId();

    // Look up the request structure. If we don't have that request active,
    // return a CANCEL so the remote system can clean up their tables.

    ReqInfo* const req = taskPool->reqPool.entry(msgId);

    if (req && (hdr.server() == req->remNode() || req->multicasted()) &&
	hdr.svrTaskName() == req->taskName() && hdr.clntTaskId() == req->task().id()) {

	// Check every 5 seconds to see if the task that's receiving replies
	// is still alive.  We throttle it because the call is too expensive
	// to do on every reply with pid-based contexts.

	if (req->task().stillAlive(DeltaTime(5000))) {

	    // Update some bookkeeping; increment packet counters and reset the
	    // time-out.

	    ++req->task().stats.rpyRcv;
	    ++taskPool->stats.rpyRcv;

	    req->bumpPktStats();
	    taskPool->reqPool.update(req);

	    // If it's not a PEND message, we process it and send it to the
	    // client. (All the expressions calculated above have a common !D
	    // component. This if-statement tests the !D component for all of the
	    // expressions.)

	    if (hdr.status() != ACNET_PEND) {
		bool const A = req->wantsMultReplies();
		bool const B = req->multicasted();
		bool const C = hdr.isEMR();

		if (A && C) {

		    // The request is multicasted for multiple replies. We set
		    // the mult bit and clear out an ENDMULT status since the
		    // link needs to stay open until cancelled.

		    if (B) {
			hdr.setFlags(hdr.flags() | ACNET_FLG_MLT);
			if (hdr.status() == ACNET_ENDMULT)
			    hdr.setStatus(ACNET_SUCCESS);
		    }

		    // If the request was not multicasted. We update the header
		    // to reflect the correct value.

		    else {
			hdr.setFlags(hdr.flags() & ~ACNET_FLG_MLT);
			if (hdr.status() == ACNET_SUCCESS)
			    hdr.setStatus(ACNET_ENDMULT);
		    }
		}

		// Send the packet to the client. If we can't talk to the client,
		// clean up the request resources.  If it's a multiple reply or a
		// multicasted request, send a cancel, as well.

		if (req->task().sendDataToClient(&hdr)) {
		    if (!A || (!B && C))
			taskPool->reqPool.cancelReqId(msgId, !A && (!C || B));
		} else {
		    syslog(LOG_WARNING, "Trouble communicating with %s, shutting "
			   "down request 0x%04x", req->task().handle().str(),
			   msgId.raw());
		    taskPool->reqPool.cancelReqId(msgId, A || B);
		}
	    }
	} else

	    // Remove the dead task and clean up it's active requests

	    taskPool->removeTask(&req->task());
    } else {
	AcnetHeader const hdr2(ACNET_FLG_CAN, ACNET_SUCCESS, hdr.server(),
			       hdr.client(), hdr.svrTaskName(),
			       hdr.clntTaskId(), msgId, sizeof(AcnetHeader));

	(void) sendDataToNetwork(hdr2, 0, 0);
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Sent CANCEL to 0x%04x for bad req id 0x%04x "
		   "from task 0x%08x", hdr.server().raw(),
		   msgId.raw(), hdr.svrTaskName().raw());
    }
}

// Looks up the IP address of the specified ACNET node and returns it or 0,
// if the node cannot be found. If the node can't be found and we don't have
// a valid IP table, then we temporarily insert the entry into our table (it
// will get overridden when NODES updates us.)

static ipaddr_t ipAddress(trunknode_t& tn, ipaddr_t const defAddress, bool tempAdd)
{
    if (tn.isBlank()) {
	if (defAddress != myIp())
	    addrLookup(defAddress, tn);
	return defAddress;
    } else {
	sockaddr_in const* const in = getAddr(tn);

	if (in)
	    return ipaddr_t(ntohl(in->sin_addr.s_addr));
	else if (!lastNodeTableDownloadTime() && tempAdd) {
	    updateAddr(tn, nodename_t(-1), defAddress);
	    syslog(LOG_WARNING, "Temporarily adding %s for node 0x%02x%02x",
		    defAddress.str().c_str(), tn.trunk().raw(), tn.node());
	    return defAddress;
	} else
	    return ipaddr_t();
    }
}

static void handleNodeTableDownloadRequest(AcnetHeader& hdr)
{
    TaskPool *taskPool = new TaskPool(hdr.server(), nodename_t());

    if (taskPool) {
	handleAcnetRequest(taskPool, hdr);
	delete taskPool;
    } else
	syslog(LOG_ERR, "unable to allocate TaskPool for node table download");
}

static TaskPool* getTaskPool(trunknode_t node)
{
    nodename_t name;

    if (nodeLookup(node, name)) {
	auto ii = taskPoolMap.find(name);

	if (ii == taskPoolMap.end()) {
	    if (isThisMachine(node)) {
		TaskPool* taskPool = new TaskPool(node, name);

		if (taskPool) {
		    taskPoolMap.insert(TaskPoolMap::value_type(name, taskPool));
		    syslog(LOG_DEBUG, "created TaskPool for node %s", name.str());
		    return taskPool;
		} else
		    syslog(LOG_ERR, "unable to allocate TaskPool for node %s", name.str());
	    }
	} else
	    return (*ii).second;
    }

    return 0;
}

static TaskPool* getTaskPool(nodename_t name)
{
    // Blank names mean the configured node for this host

    if (name.isBlank())
	return getTaskPool(myNode());

    auto ii = taskPoolMap.find(name);

    if (ii == taskPoolMap.end()) {
	trunknode_t node;

	if (nameLookup(name, node))
	    return getTaskPool(node);
    } else
	return (*ii).second;

    return 0;
}

void handleAcnetPacket(AcnetHeader& hdr, ipaddr_t const ip)
{
    uint16_t const flags = hdr.flags();
    uint16_t pktType = (flags & (0xf800 | ACNET_FLG_TYPE | ACNET_FLG_CAN | ACNET_FLG_MLT));

    // Dump the contents of the packet to the logger.

    if (dumpIncoming)
	dumpPacket("Incoming", hdr, hdr.msg(), hdr.msgLen());

    trunknode_t ctn = hdr.client();
    ipaddr_t const inClient = ipAddress(ctn, ip, pktType == ACNET_FLG_REQ);

    if (!inClient.isValid()) {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Dropping packet from %s -- bad client node 0x%02x%02x",
		   ip.str().c_str(), ctn.trunk().raw(), ctn.node());
	return;
    }

    // The client node cannot be a multicast node. We also don't allow
    // packets from multicast IP addresses.

    if (inClient.isMulticast() || ip.isMulticast()) {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Dropping packet from multicast node 0x%02x%02x (ip = %s)",
			ctn.trunk().raw(), ctn.node(), ip.str().c_str());
	return;
    }

    hdr.setClient(ctn);

    trunknode_t stn = hdr.server();
    ipaddr_t const inServer = ipAddress(stn, myIp(), pktType == ACNET_FLG_REQ);

    if (!inServer.isValid()) {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Dropping packet from %s -- bad server node 0x%02x%02x",
		    ip.str().c_str(), ctn.trunk().raw(), ctn.node());
	return;
    }

    bool mcast = inServer.isMulticast();
    TaskPool *taskPool = 0;

    switch (pktType) {
     case ACNET_FLG_USM:
	if (validFromAddress("USM", ctn, inClient, ip))
	    if (mcast || validToAddress("USM", ctn, stn)) {
		if (mcast) {
		    auto ii = taskPoolMap.begin();

		    while (ii != taskPoolMap.end())
			handleAcnetUsm((*ii++).second, hdr);
		} else if ((taskPool = getTaskPool(hdr.server())) && (taskPool->taskExists(hdr.svrTaskName()) || !defaultNodeFallback))
		    handleAcnetUsm(taskPool, hdr);
		else if ((taskPool = getTaskPool(myNode())))
		    handleAcnetUsm(taskPool, hdr);
	    }
	break;

     case ACNET_FLG_CAN:
	if (validFromAddress("CANCEL", ctn, inClient, ip))
	    if (mcast || validToAddress("CANCEL", ctn, stn)) {
		if (mcast) {
		    auto ii = taskPoolMap.begin();

		    while (ii != taskPoolMap.end())
			handleAcnetCancel((*ii++).second, hdr);
		} else if ((taskPool = getTaskPool(hdr.server())) && taskPool->rpyPool.rpyInfo(hdr.client(), hdr.msgId()))
		    handleAcnetCancel(taskPool, hdr);
		else if (defaultNodeFallback && hdr.server() != myNode() && (taskPool = getTaskPool(myNode()))) {
#ifdef DEBUG
		    syslog(LOG_NOTICE, "Passing cancel for %04x to the default node %04x", hdr.server().raw(), myNode().raw());
#endif
		    handleAcnetCancel(taskPool, hdr);
		}
	    }
	break;

     case ACNET_FLG_REQ:
     case ACNET_FLG_REQ | ACNET_FLG_MLT:
	if (!lastNodeTableDownloadTime())
	    handleNodeTableDownloadRequest(hdr);
	else if (validFromAddress("REQUEST", ctn, inClient, ip))
	    if (mcast || validToAddress("REQUEST", ctn, stn)) {
		if (mcast) {
		    auto ii = taskPoolMap.begin();

		    while (ii != taskPoolMap.end())
			handleAcnetRequest((*ii++).second, hdr);
		} else if ((taskPool = getTaskPool(hdr.server())) && (taskPool->taskExists(hdr.svrTaskName()) || !defaultNodeFallback))
		    handleAcnetRequest(taskPool, hdr);
		else if ((taskPool = getTaskPool(myNode())))
		    handleAcnetRequest(taskPool, hdr);
	    }
	break;

     case ACNET_FLG_RPY:
     case ACNET_FLG_RPY | ACNET_FLG_MLT:
	if (validFromAddress("REPLY", stn, inServer, ip))
	    if (validToAddress("REPLY", stn, ctn)) {
		taskPool = getTaskPool(hdr.client());

		if (taskPool)
		    handleAcnetReply(taskPool, hdr);
	    }
	break;

     default:
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Invalid 'flags' field in ACNET header (from %s) -- 0x%04x",
		    ip.str().c_str(), flags);
	break;
    }
}

// This function breaks the packet up (if necessary), and routes the data to
// the packet handler

static void handleNetworkDatagram(uint8_t const* const buf, ssize_t const len, ipaddr_t const ip)
{
    // If the packet is an odd length, log it and drop it.

    if (len & 1) {
      	if (dumpIncoming)
	    syslog(LOG_WARNING, "Odd-sized ACNET packet received from %s", ip.str().c_str());
	return;
    }

    size_t offset = 0;

    // We loop through (possibly) multiple acnet packets. To do this, we need
    // to have at least one AcnetHeader's worth of data. If the remaining
    // data size is less, then we're done.

    while (len - offset >= sizeof(AcnetHeader)) {
	AcnetHeader& hdr = *const_cast<AcnetHeader*>(reinterpret_cast<AcnetHeader const*>(buf + offset));
	uint16_t const pktSize = hdr.msgLen();

	// If this packet indicates more data than what is here, we log a
	// message and ignore it.

	if (!isPacketSizeValid(offset, pktSize, len, ip))
	    return;

	handleAcnetPacket(hdr, ip);
	offset += (pktSize + 1) & ~1;
    }
}

static void sendClientError(sockaddr_in const& in, status_t err)
{
    Ack ack;

    ack.setStatus(err);
    (void) sendto(sClient, &ack, sizeof(ack), 0, (sockaddr*) &in, sizeof(in));
}

static bool handleClientCommand()
{
    static char buf[64 * 1024];
    sockaddr_in in;
    socklen_t in_len = sizeof(in);
    ssize_t recvLen;

    // Make sure we were able to successfully read from the socket. If we
    // couldn't, we're in a bad state and need to report the problem (over
    // and over and over, probably.)

    if ((recvLen = recvfrom(sClient, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&in), &in_len)) > 0) {

        // Make sure the packet is at least the size of a minimum packet. If
        // it is at least the size of the CommandHeader base class, then we
        // can look at the typecode. (TP-1)

        if ((size_t) recvLen >= sizeof(CommandHeader)) {
            CommandHeader* const cmdHdr = reinterpret_cast<CommandHeader*>(buf);

	    // Check for adding a node to the node table since it doesn't
	    // require a TaskPool

	    if (CommandList::cmdAddNode == cmdHdr->cmd()) {
		Ack ack;
		AddNodeCommand const* const cmd = static_cast<AddNodeCommand const*>(cmdHdr);

		trunknode_t const node = cmd->addr();
		nodename_t const name = cmd->nodeName();
		ipaddr_t const addr = cmd->ipAddr();

		if (node.isBlank() && name.isBlank() && addr.value() == 0)
		    setLastNodeTableDownloadTime();
		else
		    updateAddr(node, name, addr);

		(void) sendto(sClient, &ack, sizeof(ack), 0, (sockaddr*) &in, in_len);
	    } else {

		// All commands at this point need a valid TaskPool

		TaskPool* const taskPool = getTaskPool(cmdHdr->virtualNodeName());

		if (!taskPool)
		    sendClientError(in, ACNET_NO_NODE);
		else if (CommandList::cmdConnect == cmdHdr->cmd() || CommandList::cmdConnectExt == cmdHdr->cmd() 
						|| CommandList::cmdTcpConnectExt == cmdHdr->cmd()) {

		    // Make sure the packet size is correct. (TP-3)

		    if ((size_t) recvLen < sizeof(ConnectCommand))
			sendClientError(in, ACNET_INVARG);
		    else
			taskPool->handleConnect(in, static_cast<ConnectCommand const*>(cmdHdr), recvLen);

		} else if (CommandList::cmdNameLookup == cmdHdr->cmd()) {

		    // Name Lookup commands don't require a valid connection
		    // either. Make sure the packet size is correct.

		    if ((size_t) recvLen != sizeof(NameLookupCommand))
			sendClientError(in, ACNET_INVARG);
		    else {
			AckNameLookup ack;
			trunknode_t addr;
			NameLookupCommand const* const cmd = static_cast<NameLookupCommand const*>(cmdHdr);

			ack.setStatus(nameLookup(cmd->name(), addr) ?
				      (ack.setTrunkNode(addr), ACNET_SUCCESS) : ACNET_NO_NODE);

			(void) sendto(sClient, &ack, sizeof(ack), 0, (sockaddr*) &in, in_len);
		    }
		}

		// Yet another command that doesn't require a valid connection.

		else if (CommandList::cmdNodeLookup == cmdHdr->cmd()) {

		    // Make sure the packet size is correct.

		    if ((size_t) recvLen != sizeof(NodeLookupCommand))
			sendClientError(in, ACNET_INVARG);
		    else {
			AckNodeLookup ack;
			nodename_t name;
			NodeLookupCommand const* const cmd = static_cast<NodeLookupCommand const*>(cmdHdr);

			ack.setStatus(nodeLookup(cmd->addr(), name) ?
				      (ack.setNodeName(name), ACNET_SUCCESS) : ACNET_NO_NODE);
			(void) sendto(sClient, &ack, sizeof(ack), 0, (sockaddr*) &in, in_len);
		    }
		}

		// Get the local node

		else if (CommandList::cmdLocalNode == cmdHdr->cmd()) {
		    AckNameLookup ack;

		    ack.setTrunkNode(taskPool->node());
		    (void) sendto(sClient, &ack, sizeof(ack), 0, (sockaddr*) &in, in_len);
		}

		// Get the default node

		else if (CommandList::cmdDefaultNode == cmdHdr->cmd()) {
		    AckNameLookup ack;

		    ack.setTrunkNode(myNode());
		    (void) sendto(sClient, &ack, sizeof(ack), 0, (sockaddr*) &in, in_len);
		}


		// All other commands require a taskname connected to
		// received port.

		else {
		    ExternalTask* const task = dynamic_cast<ExternalTask *>
					(taskPool->getTask(cmdHdr->clientName(), ntohs(in.sin_port)));

		    if (task)
			task->handleClientCommand(cmdHdr, recvLen);
		    else
			sendClientError(in, ACNET_NCN);
		}
	    }
	} else
	    syslog(LOG_WARNING, "received %d bytes (too short to be a command)", in_len);
	return true;
    } else
	return false;
}

// Determines whether the size of a packet within a (potentially) larger
// datagram is valid. Given the offset into the datagram, the packet cannot
// be larger than the remaining data. The packet cannot also be larger than
// the largest valid ACNET packet length.

static bool isPacketSizeValid(size_t const packetOffset, uint16_t const packetSize, size_t const datagramSize,
			      ipaddr_t const ip)
{
    if (packetSize < sizeof(AcnetHeader)) {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Packet (from %s) can't be smaller than AcnetHeader", ip.str().c_str());
	return false;
    }

    if (packetOffset + packetSize > datagramSize) {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "End of packet (from %s) was corrupted (header specified more "
		   "data (%d bytes) than what was sent (%d bytes))", ip.str().c_str(),
		   packetSize, (int)(datagramSize - packetOffset));
	return false;
    }

    if (packetSize > INTERNAL_ACNET_PACKET_SIZE) {
	if (dumpIncoming)
	    syslog(LOG_WARNING, "Too long packet (%d bytes > %d max) sent by %s -- skipping packet",
		   packetSize, INTERNAL_ACNET_PACKET_SIZE, ip.str().c_str());
	return false;
    }
    return true;
}

void printElapsedTime(std::ostream& os, int64_t delta)
{
    delta /= 1000;

    if (delta > 60) {
	if (delta > 3600) {
	    if (delta > 86400)
		os  << (delta / 86400) << " days, ";
	    os << ((delta / 3600) % 24) << " hours, ";
	}
	os << ((delta / 60) % 60) << " minutes, ";
    }
    os << (delta % 60) << " seconds";
}

// Release the resources we acquired.

static void releaseResources()
{
    // Close the command pipe, if it exists.

    if (-1 != sClient) {
	close(sClient);
	sClient = -1;
    }
    if (-1 != sClientTcp) {
	close(sClientTcp);
	sClientTcp = -1;
    }
    networkTerm();
}

static void sigUsr(int)
{
#ifndef NO_REPORT
    sendReport = true;
#endif
}

// This handler gets called when a HUP is sent to the process.

static void sigHup(int)
{
}

// This handler gets called when INT or TERM signals are sent to the process.

static void sigInt(int)
{
    termSignal = true;
}

// When running under normal conditions, we return an infinite delay, which
// means we don't want poll() to timeout.

static DeltaTime normalConditions()
{
    return DeltaTime::infinity;
}

// In an error condition, we simply return no delay so we can quickly exit.

static DeltaTime errorConditions()
{
    return DeltaTime::noDelay;
}

#if defined(NO_DAEMON)
static int ourDaemon(int, int)
{
    return 0;
}
#else
#if THIS_TARGET == SunOS_Target || THIS_TARGET == Darwin_Target
static int ourDaemon(int, int)
{
    int r = fork();

    if (r == 0) {
	setsid();
	umask(0);
	return 0;
    } else if (r == -1)
	return -1;
    else
	exit(0);
}
#else
#define ourDaemon(a,b)	daemon((a),(b))
#endif
#endif

// When waiting for node tables, we need to return a timeout that wakes us
// every 10 seconds.

static DeltaTime waitingForNodeTable()
{
    // Request node table download if still needed

    if (!lastNodeTableDownloadTime()) {
	static const DeltaTime tenSec(10000);
	static AbsTime lastNodeTableDownloadRequestTime =
	    now() + DeltaTime(-10000);

	DeltaTime const delta = now() - lastNodeTableDownloadRequestTime;

	if (delta >= tenSec) {
	    syslog(LOG_INFO, "Requesting node table download");
	    sendUsmToNetwork(ACNET_MULTICAST, taskhandle_t("NODES"),
			     nodename_t(), AcnetTaskId, 0, 0);
	    lastNodeTableDownloadRequestTime += tenSec;
	    return tenSec;
	} else
	    return tenSec - delta;
    } else if (!ourDaemon(1, 0)) {
	syslog(LOG_INFO, "Received node table"
#ifndef NO_DAEMON
	       " ... now running as background task"
#endif
	       ".");
	generateKillerMessages();
	nodeTableConstraints = normalConditions;
#if THIS_TARGET == NetBSD_Target
	if (pidfile("acnetd"))
	    syslog(LOG_WARNING, "couldn't create PID file -- %m");
#endif
	return DeltaTime::infinity;
    } else {
	static char const errMsg[] = "Couldn't run in the background! Terminating...";

	syslog(LOG_ERR, "%s\n", errMsg);
	fprintf(stderr, "%s\n", errMsg);
	nodeTableConstraints = errorConditions;
	termSignal = true;
	return DeltaTime::noDelay;
    }
}

#ifndef NO_REPORT
void generateReport()
{
    auto ii = taskPoolMap.begin();

    while (ii != taskPoolMap.end())
	(*ii++).second->generateReport();
}
#endif

extern void handleTcpClient(int, nodename_t);

pid_t handleClientTcpConnect()
{
    sockaddr addr;
    socklen_t addrLen = sizeof(sockaddr);

    int s = accept(sClientTcp, &addr, &addrLen);

    if (s != -1) {
	pid_t pid;

	if (!(pid = fork())) {
	    close(sNetwork);
	    close(sClient);
	    handleTcpClient(s, tcpNodeName);
	} else {
	    close(s);
	    return pid;
	}
    }

    return -1;
}

int main(int argc, char** argv)
{

#if THIS_TARGET == Darwin_Target
    openlog("acnetd", LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_LOCAL1);
#else
    openlog("acnetd", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
#endif

    // Register our signal handlers.

    signal(SIGUSR1, sigUsr);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, sigHup);
    signal(SIGINT, sigInt);
    signal(SIGTERM, sigInt);

    if (cmdLineArgs.proc(argc, argv)) {

	// Check for alternate ACNET port from the command line

	if (cmdLineArgs.alternate)
	    acnetPort = cmdLineArgs.altPort;

	if (getResources()) {
	    {
		ipaddr_t const ip = myIp();

		syslog(LOG_NOTICE, "ACNET (" THIS_PLATFORM "-" THIS_ARCH ") "
		       "services are now active on host %s:%u",
		       ip.str().c_str(), acnetPort);

		// Add our node and address if passed on the comand line

		if (!cmdLineArgs.myNode.isBlank())
		    updateAddr(cmdLineArgs.myNode, myHostName(), ip);
	    }

	    pollfd pfd[] = {
		{ sNetwork, POLLIN, 0 },
		{ sClient, POLLIN, 0 },
		{ sClientTcp, POLLIN, 0 },
	    };

	    getCurrentTime();
	    if (cmdLineArgs.standAlone)
		setLastNodeTableDownloadTime();

	    while (1) {

		// If we received a SIGUSR1, we need to generate a report.

#ifndef NO_REPORT
		if (sendReport) {
		    sendReport = false;
		    generateReport();
		}
#endif

		// If we received a termination signal, we may need to send
		// network shutdown traffic.

		if (termSignal) {
		    syslog(LOG_NOTICE, "received a termination signal");
		    termSignal = false;
		    termApp = true;

		    auto ii = taskPoolMap.begin();

		    while (ii != taskPoolMap.end())
			(*ii++).second->removeAllTasks();
		}

		// Determine the default poll timeout (based upon whether we
		// have any pending node table constraints.)

		DeltaTime pollTimeout = nodeTableConstraints();

		// Send ACNET_PENDING status for all active replies that
		// haven't generated data for a while. Return the number of
		// seconds until the next reply times out.

		auto ii = taskPoolMap.begin();

		while (ii != taskPoolMap.end()) {
		    TaskPool* taskPool = (*ii++).second;

		    std::min(pollTimeout, taskPool->reqPool.sendRequestTimeoutsAndGetNextTimeout());
#ifdef KEEP_ALIVE
		    std::min(pollTimeout, taskPool->rpyPool.sendReplyPendsAndGetNextTimeout());
#endif
		}

		// Send all pending packets destined for the network
		// interface. If sendPendingPackets() returns false, then we
		// still have outgoing packets that didn't reach the network
		// interface. We force the timeout to 20mS to give the kernel
		// some time to transmit some of the packets that have built
		// up. When we wake up again, hopefully there's enough room
		// in the network buffers.

		if (!sendPendingPackets())
		    pollTimeout = DeltaTime(20);

		// If we reached this test, then there are no outbound
		// network packets to be sent. If we need to terminate the
		// application, it is safe to do so.

		else if (termApp)
		    break;

		poll(pfd, sizeof(pfd) / sizeof(*pfd), pollTimeout.get_msec());

		getCurrentTime();

		while ((pfd[0].revents | pfd[1].revents | pfd[2].revents) & POLLIN) {
		    // Check to see if there are any client commands sent to
		    // us.

		    if ((pfd[1].revents & POLLIN) != 0)
			if (!handleClientCommand())
			    pfd[1].revents &= ~POLLIN;

		    // Look at network traffic.

		    if ((pfd[0].revents & POLLIN) != 0) {
			static uint8_t buf[INTERNAL_ACNET_PACKET_SIZE];
			sockaddr_in in;
			ssize_t len;

			if ((len = readNextPacket(buf, sizeof(buf), in)) > 0) {
			    if (in.sin_port == ntohs(acnetPort))
				handleNetworkDatagram(buf, len, ipaddr_t(ntohl(in.sin_addr.s_addr)));
			} else
			    pfd[0].revents &= ~POLLIN;
		    }
		    if (!termSignal && (pfd[2].revents & POLLIN) != 0) {
			handleClientTcpConnect();
			pfd[2].revents &= ~POLLIN;
		    }
		}
	    }
	    syslog(LOG_WARNING, "process was asked to terminate");

	    // Clean up resources.

	    releaseResources();
	} else
	    // Couldn't get network resource so we assume we already running

	    return 0;
    } else
	cmdLineArgs.showUsage();
    closelog();
    return 1;
}

// Local Variables:
// mode:c++
// fill-column:77
// End:
