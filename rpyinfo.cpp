#include <map>
#include <cstring>
#ifndef NO_REPORT
#include <iomanip>
#endif
#include "server.h"

Node ReplyRoot;

rpyid_t RpyInfo::id() const
{
    return task().taskPool().rpyPool.idPool.id(this);
}

bool RpyInfo::xmitReply(status_t status, void const* const data,
			size_t const n, bool const emr)
{
    bool repDone = false;

    // If the request wasn't ACKed yet, then the client's ACK didn't happen.
    // Log a message so we can keep track.

    if (!beenAcked())
	syslog(LOG_WARNING, "implicitly decremented the pending count for "
				"REPLY 0x%04x", id().raw());

    AcnetHeader hdr(ACNET_FLG_RPY, status, lclNode(), remNode(), taskName(), taskId(),
		    reqId(), sizeof(AcnetHeader) + MSG_LENGTH(n));

    // We handle the response differently based upon whether the reply is a
    // one-shot or multiple.

    bumpPktStats();

    if (isMultReplier()) {

	// If the application is ending a multiple reply, adjust the
	// header. First, the status field should be set to ACNET_ENDMULT, if
	// the application hasn't already set it to some error condition.

	if (emr) {
	    if (status == ACNET_SUCCESS)
		hdr.setStatus(ACNET_ENDMULT);
	    repDone = true;
	} else

	    // It's a request for multiple replies and the application doesn't
	    // want to end it, so turn on the multiple reply bit.

	    hdr.setFlags(ACNET_FLG_RPY | ACNET_FLG_MLT);
    } else
	repDone = true;

    if (!mcast)
	task().taskPool().rpyPool.update(this);

    sendDataToNetwork(hdr, data, n);

    return repDone;
}

RpyInfo* ReplyPool::getOldest()
{
    return dynamic_cast<RpyInfo*>(RpyInfo::first());
}

status_t ReplyPool::sendReplyToNetwork(TaskInfo const* const task,
				     rpyid_t const id, status_t const status,
				     void const* const data, size_t const n,
				     bool const emr)
{
    RpyInfo* const rpy = rpyInfo(id);

    if (rpy && rpy->task().equals(task)) {
	if (rpy->xmitReply(status, data, n, emr))
	    endRpyId(id);

	++task->stats.rpyXmt;
	++task->taskPool().stats.rpyXmt;

	// Return now so we don't send two replies to the client.

	return ACNET_SUCCESS;
    } else
	return ACNET_NSR;
}


RpyInfo* ReplyPool::alloc(TaskInfo* task, reqid_t msgId, taskid_t tId,
		 taskhandle_t tgtTask, trunknode_t lclNode,
		 trunknode_t remNode, uint16_t flg)
{
    assert(task);

    RpyInfo *rpy = idPool.alloc();

    rpy->task_ = task;
    rpy->reqId_ = msgId;
    rpy->taskId_ = tId;
    rpy->taskName_ = tgtTask;
    rpy->remNode_ = remNode;
    rpy->flags = flg;
    rpy->acked = false;
    rpy->initTime_ = now();
    rpy->totalPackets.reset();

    {
	sockaddr_in const* const in = getAddr(lclNode);

	rpy->mcast = in && IN_MULTICAST(htonl(in->sin_addr.s_addr));
	rpy->lclNode_ = rpy->mcast ? task->taskPool().node() : lclNode;
    }

    // Insert the data into our lookup table. If the insert was successful,
    // we're done.

    activeMap.insert(ActiveMap::value_type(request_key_t(remNode, msgId), rpy));

    try {
#ifdef PINGER
	auto const ii = targetMap.find(remNode);

	if (ii != targetMap.end())
	    ++(ii->second);
	else
	    targetMap.insert(ActiveTargetMap::value_type(remNode, 1));

	try {
#endif
	    if (task->addReply(rpy->id())) {

		// If the reply is not part of a multicast request, then we
		// add it to the timeout list so we generate periodic
		// ACNET_PEND messages.

		if (!rpy->mcast)
		    update(rpy);
#ifdef DEBUG
		syslog(LOG_DEBUG, "Created new rep (id = 0x%04x) for task "
		       "'%s' ... requestor is task %d on trunk %x, node %d "
		       "-- %d active reply structures", rpy->id(), rpy->taskName().str(),
		       rpy->taskId(), rpy->remNode().trunk(), rpy->remNode().node(),
		       (int) idPool.activeIdCount());
#endif
	    }
#ifdef PINGER
	}
	catch (...) {
	    if (!--(ii->second))
		targetMap.erase(remNode);
	    throw;
	}
#endif
    }
    catch (...) {
	activeMap.erase(request_key_t(remNode, msgId));
	throw;
    }

    return rpy;
}

void ReplyPool::release(RpyInfo *rpy)
{
#ifdef PINGER
    auto const ii = targetMap.find(rpy->remNode());

    assert(ii != targetMap.end());

    if (!--(ii->second))
	targetMap.erase(rpy->remNode());
#endif

    activeMap.erase(request_key_t(rpy->remNode(), rpy->reqId()));
    rpy->task_ = 0;
    rpy->detach();
    idPool.release(rpy);
}

RpyInfo* ReplyPool::rpyInfo(rpyid_t id)
{
    return idPool.entry(id);
}

RpyInfo* ReplyPool::rpyInfo(trunknode_t node, reqid_t id)
{
    auto ii = activeMap.find(request_key_t(node, id));

    return ii != activeMap.end() ? ii->second : 0;
}

void ReplyPool::endRpyToNode(trunknode_t const tn)
{
    RpyInfo* rpy = 0;

    while (0 != (rpy = idPool.next(rpy)))
	if (rpy->remNode() == tn) {
	    rpyid_t const id = rpy->id();

	    if (dumpOutgoing)
		syslog(LOG_INFO, "sending faked CANCEL for reply 0x%04x",
		       id.raw());

	    // Send a faked out CANCEL reply to the local client so that
	    // it can gracefully clean up its resources.

	    AcnetHeader const hdr(ACNET_FLG_CAN, ACNET_SUCCESS, rpy->lclNode(),
				  rpy->remNode(), rpy->taskName(), rpy->taskId(),
				  rpy->reqId(), sizeof(AcnetHeader));

	    TaskInfo& task = rpy->task();
	    bool failed = !task.sendDataToClient(&hdr);
	    ++task.stats.usmRcv;
	    ++task.taskPool().stats.usmRcv;

	    // Clean up our local resources.

	    (void) task.removeReply(id);
	    release(rpy);

	    if (failed)
		task.taskPool().removeTask(&task);
	    break;
	}
#ifdef DEBUG
    syslog(LOG_INFO, "Released (several) reply structures -- pool now has %d "
	   "active replies", (int) idPool.activeIdCount());
#endif
}

void ReplyPool::endRpyId(rpyid_t id, status_t status)
{
    RpyInfo* const rpy = rpyInfo(id);

    if (rpy) {
	if (!rpy->task().removeReply(rpy->id()))
	    syslog(LOG_WARNING, "didn't remove RPY ID 0x%04x from task %d", id.raw(), rpy->task().id().raw());

#ifdef DEBUG
	syslog(LOG_INFO, "END REQUEST: id = 0x%04x -- last reply was sent.", rpy->reqId());
#endif

	if (status != ACNET_SUCCESS) {

	    // If the reply is attached to a multicasted request that expects
	    // multiple replies, then we don't want to transmit the EMR
	    // (acnetd is smart enough to ignore them at the other end, but
	    // we're going to be well behaved at both ends.)

	    if (!rpy->multicasted() || !rpy->isMultReplier()) {

		// When we end a reply id, we send an EMR reply.

		AcnetHeader const hdr(ACNET_FLG_RPY, status,
					rpy->lclNode(), rpy->remNode(),
					rpy->taskName(), rpy->taskId(), rpy->reqId(),
					sizeof(AcnetHeader));

		(void) sendDataToNetwork(hdr, 0, 0);
		++rpy->task().stats.rpyXmt;
		++rpy->task().taskPool().stats.rpyXmt;
	    }

	    // Tell the local client that this request is cancelled

	    AcnetHeader const hdr(ACNET_FLG_CAN, (status_t) rpy->id().raw(),
				    rpy->lclNode(), rpy->remNode(),
				    rpy->taskName(), rpy->task().id(), rpy->reqId(),
				    sizeof(AcnetHeader));

	    (void) rpy->task().sendDataToClient(&hdr);
	    ++rpy->task().stats.usmRcv;
	    ++rpy->task().taskPool().stats.usmRcv;
	}

	// Find the entry in the active map that referrs to this reply info
	// object and erase it.

	ActiveRangeIterator ii =
	    activeMap.equal_range(request_key_t(rpy->remNode(), rpy->reqId()));

	while (ii.first != ii.second) {
	    auto const current = ii.first;

	    if (current->second == rpy) {
		activeMap.erase(current);
		break;
	    } else
		++(ii.first);
	}
	release(rpy);
    }
}

int ReplyPool::sendReplyPendsAndGetNextTimeout()
{
    RpyInfo* rpy;

    while (0 != (rpy = getOldest())) {
	int64_t const expiration = rpy->expiration();

	if (expiration <= now())
	    rpy->xmitReply(ACNET_PEND, 0, 0, false);
	else
	    return expiration - now();
    }
    return -1;
}

static bool rpyInList(RpyInfo const* const rpy, uint8_t subType,
		      uint16_t const* data, uint16_t n)
{
    switch (subType) {
     case 0:
	while (n-- > 0) {
	    uint16_t const tmp = *data++;

	    if (rpy->remNode() == trunknode_t(atohs(tmp)))
		return true;
	}
	break;

     case 1:
	while (n > 1) {
	    uint32_t const tmp = *reinterpret_cast<uint32_t const*>(data);

	    if (rpy->taskName() == taskhandle_t(atohl(tmp)))
		return true;
	    data += 2;
	    n -= 2;
	}
	break;

     case 2:
	while (n > 1) {
	    uint32_t const tmp = *reinterpret_cast<uint32_t const*>(data);

	    if (rpy->task().handle() == taskhandle_t(atohl(tmp)))
		return true;
	    data += 2;
	    n -= 2;
	}
     default:
	break;
    }
    return false;
}

void ReplyPool::fillActiveReplies(AcnetRpyList& rl, uint8_t subType, uint16_t const* data,
		       uint16_t n)
{
    RpyInfo const* rpy = 0;

    rl.total = 0;
    while (0 != (rpy = idPool.next(rpy)))
	if (!n || rpyInList(rpy, subType, data, n))
	    rl.ids[rl.total++] = htoas(rpy->id().raw());
}

bool ReplyPool::fillReplyDetail(rpyid_t id, rpyDetail* const buf)
{
    RpyInfo const* const rpy = idPool.entry(id);

#ifdef DEBUG
    syslog(LOG_DEBUG, "reply detail: looking up 0x%04x", atohs(id));
#endif

    if (rpy) {
	buf->id = htoas(id.raw());
	buf->remNode = htoas(rpy->remNode().raw());
	buf->remName = htoal(rpy->taskName().raw());
	buf->lclName = htoal(rpy->task().handle().raw());
	buf->initTime = htoal(rpy->initTime());
	buf->lastUpdate = htoal(rpy->lastUpdate / 1000);
	return true;
    } else
	return false;
}

#ifndef NO_REPORT
void ReplyPool::generateRpyReport(std::ostream& os)
{
    os << "\t\t<div class=\"section\">\n\t\t<h1>Reply ID Report</h1>\n";

    RpyInfo const* rpy = 0;

    os << "<br>Active reply IDs: " << idPool.activeIdCount();
    os << "<br>Max active reply IDs: " << idPool.maxActiveIdCount() << "<br>";

    while (0 != (rpy = idPool.next(rpy))) {

	// Buffer up the name of the remote node

	nodename_t tmp;
	char remNode[128];

	if (!nodeLookup(rpy->remNode(), tmp))
	    strcpy(remNode, "");

	tmp.str(remNode);

	os << "\t\t<table class=\"dump\">\n"
	    "\t\t\t<colgroup>\n"
	    "\t\t\t\t<col class=\"label\"/>\n"
	    "\t\t\t\t<col/>\n"
	    "\t\t\t</colgroup>\n"
	    "\t\t\t<thead>\n"
	    "\t\t\t\t<tr><td colspan=\"2\">Reply 0x" << std::hex << std::setw(4) <<
	    std::setfill('0') << rpy->id().raw() <<
	    (rpy->isMultReplier() ? " (MLT)" : "") << "</td></tr>\n"
	    "\t\t\t</thead>\n"
	    "\t\t\t<tbody>\n"
	    "\t\t\t\t<tr><td class=\"label\">Owned by task</td><td>'"<< rpy->task().handle().str() << "'</td></tr>\n"
	    "\t\t\t\t<tr class=\"even\"><td class=\"label\">Request Origin</td><td>Task " <<
	    std::setfill(' ') << std::dec <<
	    (uint32_t) rpy->taskId().raw() << " on node " << remNode << " (" << std::hex << std::setw(4) << std::setfill('0') << rpy->remNode().raw() <<
	    "), request ID 0x" << std::setw(4) << rpy->reqId().raw() << "</td></tr>\n"
	    "\t\t\t\t<tr><td class=\"label\">Started</td><td>" << std::setfill(' ') << std::dec;
	printElapsedTime(os, now() - rpy->initTime());
	os << " ago.</td></tr>\n";
	if (rpy->lastUpdate != 0) {
	    os << "<tr class=\"even\"><td class=\"label\">Last reply sent</td><td>";
	    printElapsedTime(os, now() - rpy->lastUpdate);
	    os << " ago.</td></tr>\n"
		"\t\t\t\t<tr><td class=\"label\">Sent</td><td>" << (uint32_t) rpy->totalPackets << " replies.</td></tr>\n";
	}
	os << "\t\t\t</tbody>\n"
	    "\t\t</table>\n";
    }
    os << "\t\t</div>\n";
}
#endif

// Local Variables:
// mode:c++
// fill-column:78
// End:
