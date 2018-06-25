#include <cstring>
#ifndef NO_REPORT
#include <iomanip>
#endif
#include "server.h"

ReqInfo *RequestPool::alloc(TaskInfo* task, taskhandle_t taskName,
			    trunknode_t lclNode, trunknode_t remNode,
			    uint16_t flags, uint32_t tmo)
{
    assert(task);

    ReqInfo *req = idPool.alloc();

    req->task_ = task;
    req->taskName_ = taskName;
    req->lclNode_ = lclNode;
    req->remNode_ = remNode;
    req->flags = flags;
    req->tmoMs = std::min(std::max(tmo, 400u), REQUEST_TIMEOUT * 1000u);
    req->initTime_ = now();
    req->totalPackets.reset();

    {
	sockaddr_in const* const in = getAddr(remNode);

	req->mcast = in && IN_MULTICAST(htonl(in->sin_addr.s_addr));
    }

    if (task->addRequest(idPool.id(req)))
	update(req);
    else {
	free(req);
	throw std::logic_error("owning task already has this request ID");
    }

    return req;
}

void RequestPool::release(ReqInfo *req)
{
    req->detach();
    idPool.release(req);
}

reqid_t ReqInfo::id() const
{
    return task().taskPool().reqPool.idPool.id(this);
}

ReqInfo* RequestPool::oldest()
{
    Node* const tmp = root.next();

    return tmp != &root ? dynamic_cast<ReqInfo*>(tmp) : 0;
}

void RequestPool::cancelReqToNode(trunknode_t const tn)
{
    ReqInfo* req = 0;

    while (0 != (req = idPool.next(req)))
	if (req->remNode() == tn) {
	    if (dumpOutgoing)
		syslog(LOG_INFO, "sending faked EMR for request 0x%04x", req->id().raw());

	    // Send a faked out EMR reply to the local client so that it can gracefully clean up its resources.

	    AcnetHeader const hdr(ACNET_FLG_RPY, ACNET_DISCONNECTED, req->remNode(), req->lclNode(), req->taskName(),
				  req->task().id(), req->id(), sizeof(AcnetHeader));

	    TaskInfo& task = req->task();
	    bool failed = !task.sendDataToClient(&hdr);
	    ++task.stats.rpyRcv;
	    ++task.taskPool().stats.rpyRcv;

	    // Clean up our local resources associated with the request.

	    (void) req->task().removeRequest(req->id());
	    release(req);

	    if (failed)
		task.taskPool().removeTask(&task);
	    break;
	}
#ifdef DEBUG
    syslog(LOG_INFO, "Released several request structures -- %d active requests remaining", (int) idPool.activeIdCount());
#endif
}

// Free given request id

bool RequestPool::cancelReqId(reqid_t id, bool xmt, bool sendLastReply)
{
    ReqInfo* const req = idPool.entry(id);

    if (req) {
	if (!req->task().removeRequest(id))
	    syslog(LOG_WARNING, "didn't remove REQ ID 0x%04x from task %d", id.raw(), req->task().id().raw());

	if (xmt) {

	    // When we cancel a request id, we send a cancel USM.

	    AcnetHeader const hdr(ACNET_FLG_CAN, ACNET_SUCCESS, req->remNode(),
				  req->lclNode(), req->taskName(),
				  req->task().id(), id, sizeof(AcnetHeader));

	    (void) (sendDataToNetwork(hdr, 0, 0));
	    ++req->task().stats.usmXmt;
	    ++req->task().taskPool().stats.usmXmt;

	    if (sendLastReply) {
		// Tell the requestor that the open request is over

		AcnetHeader const hdr(ACNET_FLG_RPY, ACNET_DISCONNECTED,
					req->remNode(), req->lclNode(),
					req->taskName(), req->task().id(), req->id(),
					sizeof(AcnetHeader));

		(void) req->task().sendDataToClient(&hdr);
		++req->task().stats.rpyRcv;
		++req->task().taskPool().stats.rpyRcv;
	    }
	}
#ifdef DEBUG
	syslog(LOG_INFO, "CANCEL REQUEST: id = 0x%04x -- %s packet transmitted.", id, xmt ? "CANCEL" : "no");
#endif
	req->task_ = 0;
	release(req);
	return true;
    }
    return false;
}

int RequestPool::sendRequestTimeoutsAndGetNextTimeout()
{
    ReqInfo* req;

    while (0 != (req = oldest())) {
	int64_t const expiration = req->expiration();

	if (expiration <= now()) {
	    const bool mult = req->wantsMultReplies();
	    const uint16_t flags = mult ? (ACNET_FLG_RPY | ACNET_FLG_MLT) : ACNET_FLG_RPY;
	    const status_t status = mult ? ACNET_PEND : ACNET_TMO;

	    AcnetHeader const hdr(flags, status, req->remNode(), req->lclNode(),
				  req->taskName(), req->task().id(), req->id(), sizeof(AcnetHeader));
#ifdef DEBUG
	    syslog(LOG_DEBUG, "Time-out waiting for reply for request 0x%04x ...  cancelling", req->id());
#endif
	    TaskInfo& task = req->task();
	    bool failed = !task.sendDataToClient(&hdr);

	    ++task.stats.rpyRcv;
	    ++task.taskPool().stats.rpyRcv;

	    if (!mult)
		cancelReqId(req->id(), true);
	    else
		req->update(&root);

	    if (failed)
		task.taskPool().removeTask(&task);
	} else
	    return expiration - now();
    }
    return -1;
}

static bool reqInList(ReqInfo const* const req, uint8_t subType, uint16_t const* data, uint16_t n)
{
    switch (subType) {
     case 0:
	while (n-- > 0) {
	    uint16_t const tmp = *data++;

	    if (req->remNode() == trunknode_t(atohs(tmp)))
		return true;
	}
	break;

     case 1:
	while (n > 1) {
	    uint32_t const tmp = *reinterpret_cast<uint32_t const*>(data);

	    if (req->taskName() == taskhandle_t(atohl(tmp)))
		return true;
	    data += 2;
	    n -= 2;
	}
	break;

     case 2:
	while (n > 1) {
	    uint32_t const tmp = *reinterpret_cast<uint32_t const*>(data);

	    if (req->task().handle() == taskhandle_t(atohl(tmp)))
		return true;
	    data += 2;
	    n -= 2;
	}
     default:
	break;
    }
    return false;
}

void RequestPool::fillActiveRequests(AcnetReqList& rl, uint8_t subType, uint16_t const* data, uint16_t n)
{
    ReqInfo const* req = 0;

    rl.total = 0;
    while (0 != (req = idPool.next(req)))
	if (!n || reqInList(req, subType, data, n))
	    rl.ids[rl.total++] = htoas(req->id().raw());
}

bool RequestPool::fillRequestDetail(reqid_t id, reqDetail* const buf)
{
    ReqInfo const* const req = idPool.entry(id);

#ifdef DEBUG
    syslog(LOG_DEBUG, "request detail: looking up 0x%04x", atohs(id));
#endif
    if (req) {
	buf->id = htoas(id.raw());
	buf->remNode = htoas(req->remNode().raw());
	buf->remName = htoal(req->taskName().raw());
	buf->lclName = htoal(req->task().handle().raw());
	buf->initTime = htoal(req->initTime());
	buf->lastUpdate = htoal(req->lastUpdate / 1000);
	return true;
    } else
	return false;
}

#ifndef NO_REPORT
void RequestPool::generateReqReport(std::ostream& os)
{
    os << "\t\t<div class=\"section\">\n\t\t<h1>Request ID Report</h1>\n";

    ReqInfo const* req = 0;

    os << "<br>Max active request IDs: " << idPool.maxActiveIdCount() << "<br>";

    while (0 != (req = idPool.next(req))) {

	// Buffer up the name of the remote node

	nodename_t tmp;
	char remNode[128];

	if (!nodeLookup(req->remNode(), tmp))
	    strcpy(remNode, "");

	tmp.str(remNode);

	os << "\t\t<table class=\"dump\">\n"
	    "\t\t\t<colgroup>\n"
	    "\t\t\t\t<col class=\"label\"/>\n"
	    "\t\t\t\t<col/>\n"
	    "\t\t\t</colgroup>\n"
	    "\t\t\t<thead>\n"
	    "\t\t\t\t<tr><td colspan=\"2\">Request 0x" << std::hex << std::setw(4) <<
	    std::setfill('0') <<
	    req->id().raw() << (req->wantsMultReplies() ? " (MLT)" : "") << "</td></tr>\n"
	    "\t\t\t</thead>\n"
	    "\t\t\t<tbody>\n"
	    "\t\t\t\t<tr><td class=\"label\">Owned by task</td><td>'"<<
	    req->task().handle().str() << "'</td></tr>\n";
	os << "\t\t\t\t<tr class =\"even\"><td class=\"label\">Request Target</td><td>Task '" <<
	    req->taskName().str() << "' on node " << remNode << " (" << std::hex <<
	    std::setw(4) << std::setfill('0') << req->remNode().raw() <<
	    ")</td></tr>\n"
	    "\t\t\t\t<tr><td class=\"label\">Started</td><td>" << std::setfill(' ') << std::dec;

	printElapsedTime(os, now() - req->initTime());

	os << " ago.</td></tr>\n";

	if (req->lastUpdate != 0) {
	    os << "\t\t\t\t<tr class=\"even\"><td class=\"label\">Last reply received</td><td>";
	    printElapsedTime(os, now() - req->lastUpdate);
	    os << " ago.</td></tr>\n"
		"<tr><td class=\"label\">Received</td><td>" << (uint32_t) req->totalPackets << " replies.</td></tr>\n";
	}
	os << "\t\t\t</tbody>\n"
	    "\t\t<table>\n";
    }
    os << "\t\t</div>\n";
}
#endif

// Local Variables:
// mode:c++
// fill-column:125
// End:
