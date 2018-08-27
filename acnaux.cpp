#include <sys/time.h>
#include <cstring>
#include "server.h"

static int64_t bootTime = now();

void sendKillerMessage(trunknode_t const addr)
{
    static taskhandle_t const task(ator("ACNET"));
    uint16_t const data[] = { htoas(0x20b), htoas(1), htoas(addr.raw()) };

    sendUsmToNetwork(ACNET_MULTICAST, task, nodename_t(), AcnetTaskId,
		     reinterpret_cast<uint8_t const*>(data), sizeof(data));
}

AcnetTask::AcnetTask(TaskPool& taskPool, taskid_t id) :
    InternalTask(taskPool, taskhandle_t(ator("ACNET")), id)
{
}

void AcnetTask::versionHandler(rpyid_t id)
{
    static uint16_t const rpy[] =
	{ htoas(ACNET_PROTOCOL), htoas(ACNET_INTERNALS), htoas(ACNET_API) };

    sendLastReply(id, ACNET_SUCCESS, rpy, sizeof(rpy));
}

void AcnetTask::packetCountHandler(rpyid_t id)
{
    struct {
	uint32_t pktCount;
	time48_t time;
    } __attribute__((packed)) rpy;

    StatCounter sum;

    sum += taskPool().stats.usmXmt;
    sum += taskPool().stats.reqXmt;
    sum += taskPool().stats.rpyXmt;
    sum += taskPool().stats.usmRcv;
    sum += taskPool().stats.reqRcv;
    sum += taskPool().stats.rpyRcv;

    rpy.pktCount = htoal(sum);

    // If the current time is less than the boot time, then the system
    // administrator has adjusted the system time and we've lost all
    // information of when we started. If this happens, just set the
    // boot time to the current time.

    if (now() < bootTime)
	bootTime = now();

    toTime48(now() - bootTime, &rpy.time);
    sendLastReply(id, ACNET_SUCCESS, &rpy, sizeof(rpy));
}

void AcnetTask::taskIdHandler(rpyid_t id, uint16_t const* const data, uint16_t dataSize)
{
    if (dataSize >= sizeof(taskhandle_t) / 2) {
	nodename_t tmp;
	taskhandle_t taskName(atohl(*(uint32_t*) data));

	auto const ii = taskPool().tasks(taskName);

	if (ii.first != ii.second) {
	    uint16_t const rpy = htoas(ii.first->second->id().raw());

	    sendLastReply(id, ACNET_SUCCESS, &rpy, sizeof(rpy));
	} else
	    sendLastReply(id, ACNET_NOTASK);
    } else
	sendLastReply(id, ACNET_LEVEL2);
}

void AcnetTask::taskNameHandler(rpyid_t id, uint8_t subType)
{
    TaskInfo const* const tmp = taskPool().getTask(taskid_t((uint16_t) subType));

    if (tmp) {
	uint32_t const rpy = htoal(tmp->handle().raw());

	sendLastReply(id, ACNET_SUCCESS, &rpy, sizeof(rpy));
    } else
	sendLastReply(id, ACNET_NOTASK);
}

void AcnetTask::taskNameHandler(rpyid_t id, uint16_t const* const data, uint16_t dataSize)
{
    if (dataSize == sizeof(taskid_t) / 2) {
	TaskInfo const* const tmp = taskPool().getTask(taskid_t(atohs(data[0])));

	if (tmp) {
	    uint32_t const rpy = htoal(tmp->handle().raw());

	    sendLastReply(id, ACNET_SUCCESS, &rpy, sizeof(rpy));
	} else
	    sendLastReply(id, ACNET_NOTASK);
    } else
	sendLastReply(id, ACNET_LEVEL2);
}

void AcnetTask::killerMessageHandler(rpyid_t id, uint8_t subType, uint16_t const* const data, uint16_t dataSize)
{
    // Respond to the requestor task first, since the killer message
    // may destroy our reply ID!

    sendLastReply(id, subType == 2 ? ACNET_SUCCESS : ACNET_LEVEL2);

    // Validate the killer message packet.

    if (subType == 2) {
	if (dataSize >= 2) {
	    uint16_t const count = atohs(data[0]);

	    if (dataSize == 1 + count) {
		for (size_t ii = 0; ii < count; ++ii) {
		    trunknode_t const tn(atohs(data[1 + ii]));

		    cancelReqToNode(tn);
		    endRpyToNode(tn);
		}
	    } else if (dumpIncoming)
		syslog(LOG_WARNING, "killer message size %d ... should be "
		       "%d bytes -- ignoring.", dataSize * 2, (1 + count) * 2);
	} else if (dumpIncoming)
	    syslog(LOG_WARNING, "killer message size %d ... needs to be at "
		   "least 4 bytes -- ignoring.", dataSize * 2);
    }
}

void AcnetTask::tasksHandler(rpyid_t id, uint8_t subType)
{
    uint16_t rpy[32 * 1024];

    sendLastReply(id, ACNET_SUCCESS, rpy + 1, taskPool().fillBufferWithTaskInfo(subType, rpy + 1));
}

void AcnetTask::pingHandler(rpyid_t id)
{
    uint16_t pingRet = 0;

    sendLastReply(id, ACNET_SUCCESS, &pingRet, sizeof(pingRet));
}

void AcnetTask::taskResourcesHandler(rpyid_t id)
{
    uint16_t rpy[5];

    rpy[0] = htoas(0);
    rpy[1] = htoas(0);
    rpy[2] = htoas(taskPool().activeCount());
    rpy[3] = htoas(taskPool().rumHandleCount());
    rpy[4] = htoas(taskPool().requestCount() + taskPool().replyCount());

    sendLastReply(id, ACNET_SUCCESS, rpy, sizeof(rpy));
}

void AcnetTask::resetStats()
{
    statTimeBase = now();
    taskPool().stats.usmXmt.reset();
    taskPool().stats.reqXmt.reset();
    taskPool().stats.rpyXmt.reset();
    taskPool().stats.usmRcv.reset();
    taskPool().stats.reqRcv.reset();
    taskPool().stats.rpyRcv.reset();
}

void AcnetTask::nodeStatsHandler(rpyid_t id, uint8_t subType)
{
    struct {
	time48_t time;
	uint16_t count[10];
    } __attribute__((packed)) rpy;

    if (now() < statTimeBase)
	resetStats();

    toTime48(now() - statTimeBase, &rpy.time);

    rpy.count[0] = htoas(0);
    rpy.count[1] = htoas(0);
    rpy.count[2] = htoas(0);
    rpy.count[3] = htoas(0);
    rpy.count[4] = htoas((uint16_t) taskPool().stats.usmXmt);
    rpy.count[5] = htoas((uint16_t) taskPool().stats.reqXmt);
    rpy.count[6] = htoas((uint16_t) taskPool().stats.rpyXmt);
    rpy.count[7] = htoas((uint16_t) taskPool().stats.usmRcv);
    rpy.count[8] = htoas((uint16_t) taskPool().stats.reqRcv);
    rpy.count[9] = htoas((uint16_t) taskPool().stats.rpyRcv);

    if (subType)
	resetStats();

    sendLastReply(id, ACNET_SUCCESS, &rpy, sizeof(rpy));
}

void AcnetTask::tasksStatsHandler(rpyid_t id, uint8_t subType)
{
    uint16_t rpy[INTERNAL_ACNET_USER_PACKET_SIZE];

    sendLastReply(id, ACNET_SUCCESS, rpy, taskPool().fillBufferWithTaskStats(subType, rpy));
}

void AcnetTask::ipNodeTableHandler(rpyid_t id, uint8_t subType, uint16_t const* const data, size_t dataSize)
{
    #define WRITE_FLG		(0x80)
    #define SINGLE_FLG		(0x40)
    #define ACNET_MIN_TRUNK	(9)

    size_t trunkIndex = subType & 0x0F;

    if (subType & WRITE_FLG) {
	if (dataSize >= 1) {
	    if (subType & SINGLE_FLG) {
	    } else {
		size_t const numEntries = atohs(data[0]);

		if (trunkIndex == 0 && numEntries == 0) {
		    setLastNodeTableDownloadTime();
		    sendLastReply(id, ACNET_SUCCESS);
		    return;
		} else if (numEntries <= 256) {
		    uint32_t const* const addr = (uint32_t*) (data + 1);

		    if (((dataSize - 1) / 2) == numEntries * 2) {
			uint32_t const* const name = (uint32_t*) (addr + numEntries);

			sendLastReply(id, ACNET_SUCCESS);
			for (size_t ii = 0; ii < numEntries; ++ii)
			    updateAddr(trunknode_t(trunk_t(trunkIndex + ACNET_MIN_TRUNK),
						   (node_t) ii),
				       nodename_t(atohl(name[ii])),
				       ipaddr_t(ntohl(addr[ii])));
			return;
		    } else if (((dataSize - 1) / 2) == numEntries) {
			// Handle the older apps that only send ip addresses

			sendLastReply(id, ACNET_SUCCESS);
			for (size_t ii = 0; ii < numEntries; ++ii)
			    updateAddr(trunknode_t((trunk_t) (trunkIndex + ACNET_MIN_TRUNK),
						   (node_t) ii),
				       nodename_t(), ipaddr_t(ntohl(addr[ii])));
			return;
		    }
		}
	    }
	}
	sendLastReply(id, ACNET_LEVEL2);
    } else {
	if (subType & SINGLE_FLG) {
	    if (dataSize >= 1 && atohs(data[0]) < 256) {
		uint32_t addr;

		sockaddr_in const* sa = getAddr(trunknode_t(trunk_t(9 + trunkIndex),
							    node_t(size_t(atohs(data[0])))));
		addr = sa ? sa->sin_addr.s_addr : 0;
		sendLastReply(id, ACNET_SUCCESS, &addr, sizeof(addr));
	    } else
		sendLastReply(id, ACNET_LEVEL2);
	} else {
	    uint32_t addr[256];

	    if (trunkExists(trunk_t(9 + trunkIndex))) {
		for (size_t ii = 0; ii < 256; ii++) {
		    sockaddr_in const* sa = getAddr(trunknode_t(trunk_t(9 + trunkIndex), node_t(ii)));

		    addr[ii] = sa ? sa->sin_addr.s_addr : 0;
		}
		sendLastReply(id, ACNET_SUCCESS, addr, sizeof(addr));
	    } else
		sendLastReply(id, ACNET_LEVEL2);
	}
    }
}

void AcnetTask::timeHandler(rpyid_t id, uint8_t subType)
{
    if (subType == 1) {
	uint16_t rpy[8];
	time_t const tt = (time_t) (now() / 1000);
	struct tm* const t = localtime(&tt);

	rpy[0] = htoas(t->tm_year);
	rpy[1] = htoas(t->tm_mon + 1);
	rpy[2] = htoas(t->tm_mday);
	rpy[3] = htoas(t->tm_hour);
	rpy[4] = htoas(t->tm_min);
	rpy[5] = htoas(t->tm_sec);
	rpy[6] = htoas(now() / 100);
	rpy[7] = htoas(100);

	sendLastReply(id, ACNET_SUCCESS, rpy, sizeof(rpy));
    } else
	sendLastReply(id, ACNET_LEVEL2);
}

bool AcnetTask::sendMessageToClients(AcnetClientMessage* msg) const
{
    bool foundOne = false;
    auto ii = taskPool().tasks(msg->task());

    while (ii.first != ii.second) {
	if (ii.first->second->sendMessageToClient(msg))
	    foundOne = true;
	++ii.first;
    }
    return foundOne;
}

void AcnetTask::debugHandler(rpyid_t id, uint8_t subType, uint16_t const* const data, uint16_t dataSize)
{
    status_t status = ACNET_SUCCESS;

    switch (subType) {
     case 1:
	dumpIncomingAcnetPackets(true);
	break;

     case 2:
	dumpOutgoingAcnetPackets(true);
	break;

     case 3:
	dumpIncomingAcnetPackets(true);
	dumpOutgoingAcnetPackets(true);
	break;

     case 4:
	dumpIncomingAcnetPackets(false);
	break;

     case 5:
	dumpOutgoingAcnetPackets(false);
	break;

     case 6:
	dumpIncomingAcnetPackets(false);
	dumpOutgoingAcnetPackets(false);
	break;

     case 7:
	if (dataSize == 2) {
	    AcnetClientMessage cMsg(taskhandle_t(*(uint32_t*) data),
				    AcnetClientMessage::DumpTaskIncomingPacketsOn);
	    if (!sendMessageToClients(&cMsg))
		status = ACNET_LEVEL2;
	} else
	    status = ACNET_LEVEL2;
	break;

     case 8:
	if (dataSize == 2) {
	    AcnetClientMessage cMsg(taskhandle_t(*(uint32_t*) data),
				    AcnetClientMessage::DumpTaskIncomingPacketsOff);
	    if (!sendMessageToClients(&cMsg))
		status = ACNET_LEVEL2;
	} else
	    status = ACNET_LEVEL2;
	break;

     case 9:
	if (dataSize == 2) {
	    AcnetClientMessage cMsg(taskhandle_t(*(uint32_t*) data),
				    AcnetClientMessage::DumpProcessIncomingPacketsOn);
	    if (!sendMessageToClients(&cMsg))
		status = ACNET_LEVEL2;
	} else
	    status = ACNET_LEVEL2;
	break;

     case 10:
	if (dataSize == 2) {
	    AcnetClientMessage cMsg(taskhandle_t(*(uint32_t*) data),
				    AcnetClientMessage::DumpProcessIncomingPacketsOff);
	    if (!sendMessageToClients(&cMsg))
		status = ACNET_LEVEL2;
	} else
	    status = ACNET_LEVEL2;
	break;

     default:
	status = ACNET_LEVEL2;
	break;
    }

    sendLastReply(id, status);
}

void AcnetTask::activeReplies(rpyid_t id, uint8_t subType, uint16_t const* const data, size_t dataLen)
{
    AcnetRpyList rl;

    taskPool().rpyPool.fillActiveReplies(rl, subType, data, dataLen);
    sendLastReply(id, ACNET_SUCCESS, rl.ids, sizeof(*rl.ids) * rl.total);
}

void AcnetTask::activeRequests(rpyid_t id, uint8_t subType, uint16_t const* const data, size_t dataLen)
{
    AcnetReqList rl;

    taskPool().reqPool.fillActiveRequests(rl, subType, data, dataLen);
    sendLastReply(id, ACNET_SUCCESS, rl.ids, sizeof(*rl.ids) * rl.total);
}

void AcnetTask::replyDetail(rpyid_t id, uint16_t const* const data, size_t dataLen)
{
    rpyDetail dl[16];
    size_t ii = 0;
    size_t total = 0;
    status_t status = ACNET_SUCCESS;

    while (ii < dataLen) {
	if (total == sizeof(dl) / sizeof(*dl)) {
	    status = ACNET_TRP;
	    break;
	}
	if (taskPool().rpyPool.fillReplyDetail(rpyid_t(data[ii++]), dl + total))
	    ++total;
    }
    sendLastReply(id, status, dl, sizeof(*dl) * total);
}

void AcnetTask::requestDetail(rpyid_t id, uint16_t const* const data, size_t dataLen)
{
    reqDetail dl[16];
    size_t ii = 0;
    size_t total = 0;
    status_t status = ACNET_SUCCESS;

    while (ii < dataLen) {
	if (total == sizeof(dl) / sizeof(*dl)) {
	    status = ACNET_TRP;
	    break;
	}
	if (taskPool().reqPool.fillRequestDetail(reqid_t(data[ii++]), dl + total))
	    ++total;
    }
    sendLastReply(id, status, dl, sizeof(*dl) * total);
}

void AcnetTask::requestReport(rpyid_t id)
{
#ifndef NO_REPORT
    static int64_t lastReport = 0;

    if (now() - lastReport > 60000) {
	lastReport = now();
	generateReport();
	sendLastReply(id, ACNET_SUCCESS);
    } else
	sendLastReply(id, ACNET_BUSY);
#else
    sendLastReply(id, ACNET_LEVEL2);
#endif
}

bool AcnetTask::sendDataToClient(AcnetHeader const* hdr)
{
    uint16_t const flg = hdr->flags();

    // This is where we handle REQUESTS.

    if (PKT_IS_REQUEST(flg) || PKT_IS_USM(flg)) {
	uint16_t const* const msg = (uint16_t*) hdr->msg();
	uint16_t const size = hdr->msgLen() - sizeof(AcnetHeader);
	rpyid_t id = rpyid_t(htoas(hdr->status().raw()));

	// The data size has to be a multiple of two (historically
	// the diagnostics assume an array of 16-bit values) and
	// needs at least one uint16_t to indicate the type and
	// subtype codes.

	if (!(size & 1) && size >= 2) {
	    int8_t const type = (int8_t) atohs(msg[0]) & 0xff;
	    uint8_t subType = atohs(msg[0]) >> 8;
	    size_t dataLen = (size - 2) / 2;
	    uint16_t const* const data = msg + 1;;

	    switch (type) {
	     case -7:
		requestReport(id);
		break;

	     case -6:
		replyDetail(id, data, dataLen);
		break;

	     case -5:
		requestDetail(id, data, dataLen);
		break;

	     case -4:
		activeReplies(id, subType, data, dataLen);
		break;

	     case -3:
		activeRequests(id, subType, data, dataLen);
		break;

	     case -2:
		debugHandler(id, subType, data, dataLen);
		break;

	     case -1:
		timeHandler(id, subType);
		break;

	     case 0:
		pingHandler(id);
		break;

	     case 1:
		taskIdHandler(id, data, dataLen);
		break;

	     case 2:
		taskNameHandler(id, subType);
		break;

	     case 3:
		versionHandler(id);
		break;

	     case 4:
		tasksHandler(id, subType);
		break;

	     case 5:
		taskResourcesHandler(id);
		break;

	     case 6:
		nodeStatsHandler(id, subType);
		break;

	     case 7:
		tasksStatsHandler(id, subType);
		break;

	     case 9:
		packetCountHandler(id);
		break;

	     case 11:
		killerMessageHandler(id, subType, data, dataLen);
		break;

	     case 17:
		ipNodeTableHandler(id, subType, data, dataLen);
		break;

	     case 18:
		taskNameHandler(id, data, dataLen);
		break;

	     default:
		syslog(LOG_ERR, "Unsupported ACNET type code: %d", (int16_t) type);
		sendLastReply(id, ACNET_LEVEL2);
		break;
	    }
	} else {
	    if (dumpIncoming)
		syslog(LOG_ERR, "Invalid ACNET task request size: %u", size);

	    sendLastReply(id, ACNET_LEVEL2);
	}
    } else if (dumpIncoming)
	syslog(LOG_INFO, "The ACNET task received a non-request");

    return true;
}

// Local Variables:
// mode:c++
// End:
