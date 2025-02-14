#include <signal.h>
#include <cerrno>
#include <sys/socket.h>
#include "exttask.h"

ExternalTask::ExternalTask(TaskPool& taskPool, taskhandle_t handle, taskid_t id, pid_t pid, uint16_t cmdPort,
			    uint16_t dataPort) : TaskInfo(taskPool, handle, id),
			    pid_(pid), contSocketErrors(0), totalSocketErrors(0),
			    lastCommandTime(now()), lastAliveCheckTime(now())
{
#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
    saCmd.sin_len = sizeof(saCmd);
#endif
    saCmd.sin_family = AF_INET;
    saCmd.sin_port = htons(cmdPort);
    saCmd.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
    saData.sin_len = sizeof(saData);
#endif
    saData.sin_family = AF_INET;
    saData.sin_port = htons(dataPort);
    saData.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

bool ExternalTask::equals(TaskInfo const* task) const
{
    ExternalTask const* const o = dynamic_cast<ExternalTask const*>(task);

    return o && (commandPort() == o->commandPort());
}

bool ExternalTask::checkResult(ssize_t const res)
{
    if (res == -1) {

	// At this point we've had an error occur so lets
	// check to see if we think the client is still alive

	++stats.lostPkt;
	return stillAlive();
    }
    return true;
}

bool ExternalTask::sendDataToClient(AcnetHeader const* const hdr)
{
    ssize_t const res = sendto(sClient, hdr, hdr->msgLen(), 0,
			       (sockaddr const*) &saData, sizeof(saData));

    if (res != hdr->msgLen()) {
	contSocketErrors++;
	totalSocketErrors++;
	syslog(LOG_WARNING, "error writing to client's data socket -- %m");
    } else
	contSocketErrors = 0;

    return checkResult(res);
}

bool ExternalTask::sendAckToClient(void const* d, size_t n)
{
    ssize_t const res = sendto(sClient, d, n, 0, (sockaddr const*) &saCmd,
			       sizeof(saCmd));

    if (res != (ssize_t) n) {
	contSocketErrors++;
	totalSocketErrors++;
	syslog(LOG_WARNING, "error writing to client's command socket -- %m");
    } else
	contSocketErrors = 0;

    return checkResult(res);
}

bool ExternalTask::sendMessageToClient(AcnetClientMessage* msg)
{
    msg->setPid(pid());

    ssize_t const res = sendto(sClient, msg, sizeof(AcnetClientMessage), 0,
			       (sockaddr const*) &saData, sizeof(saData));

    if (res != sizeof(AcnetClientMessage)) {
	contSocketErrors++;
	totalSocketErrors++;
	syslog(LOG_WARNING, "error writing to client's data socket -- %m");
    } else
	contSocketErrors = 0;

    return checkResult(res);
}

bool ExternalTask::sendErrorToClient(status_t err)
{
    Ack ack;

    ack.setStatus(err);
    return sendAckToClient(&ack, sizeof(ack));
}

bool ExternalTask::stillAlive(int throttle) const
{
    if ((now() - lastAliveCheckTime) >= throttle) {
	lastAliveCheckTime = now();

	if (pid()) {
	    if (contSocketErrors > 10)
		return false;
	    else if (0 != kill(pid(), 0) && errno == ESRCH)
		return false;
	} else
	    return (now() - lastCommandTime) < 30000;
    }

    return true;
}

void ExternalTask::handleKeepAlive()
{
    Ack ack;

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleDisconnect()
{
    Ack ack;

    ack.setStatus(ACNET_SUCCESS);
    sendAckToClient(&ack, sizeof(ack));

    // Remove the task and all resource used by the connection. We do this after responding because it cannot fail (the
    // client wants to exit) and this way the client doesn't have to wait around.

    taskPool().removeTask(this);
}

void ExternalTask::handleDisconnectSingle()
{
    Ack ack;

    ack.setStatus(ACNET_SUCCESS);
    sendAckToClient(&ack, sizeof(ack));

    // Remove the task and all resource used by the connection. We do this after responding because it cannot fail (the
    // client wants to exit) and this way the client doesn't have to wait around.

    taskPool().removeOnlyThisTask(this);
}

void ExternalTask::handleSend(SendCommand const *cmd, size_t const len)
{
    Ack ack;

    // Now route the packet outside the box.

    if (len >= sizeof(SendCommand) && len <= INTERNAL_ACNET_USER_PACKET_SIZE + sizeof(SendCommand)) {
	trunknode_t node = cmd->addr();

	if (node.isBlank())
	    node = taskPool().node();

	if (getAddr(node)) {
	    sendUsmToNetwork(node, cmd->task(), taskPool().nodeName(),
			     id(), cmd->data(), len - sizeof(SendCommand));
	    ++stats.usmXmt;
	    ++taskPool().stats.usmXmt;
	} else
	    ack.setStatus(ACNET_NO_NODE);
    } else
	ack.setStatus(ACNET_IVM);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleReceiveRequests()
{
    if (!sendErrorToClient(ACNET_IVM))
	taskPool().removeTask(this);
}

void ExternalTask::handleBlockRequests()
{
    if (!sendErrorToClient(ACNET_IVM))
	taskPool().removeTask(this);
}

void ExternalTask::handleSendRequest(SendRequestCommand const *cmd, size_t const len)
{
    AckSendRequest ack;

    // In order for the message to be sent, the payload must not be bigger than INTERNAL_ACNET_USER_PACKET (which means the
    // message cannot be bigger than the max payload size plus the command header.)

    if (len >= sizeof(SendRequestCommand) && len <= INTERNAL_ACNET_USER_PACKET_SIZE + sizeof(SendRequestCommand)) {
	trunknode_t node = cmd->addr();

	if (node.isBlank())
	    node = taskPool().node();

	if (getAddr(node)) {
	    try {
		RequestPool& reqPool = taskPool().reqPool;

		// Try to allocate a new request ID. If we're successful, send the data to the remote machine.

		ReqInfo* const req = reqPool.alloc(this, cmd->task(), taskPool().node(), node, cmd->flags(), REQUEST_TIMEOUT * 1000u);
		size_t const msgLen = len - sizeof(SendRequestCommand);
		AcnetHeader const hdr(ACNET_FLG_REQ | ((cmd->flags() & REQ_M_MULTRPY) ? ACNET_FLG_MLT : 0), ACNET_SUCCESS,
				      node, taskPool().node(), cmd->task(), id(), req->id(),
				      sizeof(AcnetHeader) + MSG_LENGTH(msgLen));

		sendDataToNetwork(hdr, cmd->data(), msgLen);
		++stats.reqXmt;
		++taskPool().stats.reqXmt;
		ack.setRequestId(req->id());
	    } catch (...) {
		ack.setStatus(ACNET_NLM);
		++taskPool().stats.reqQLimit;
	    }
	} else
	    ack.setStatus(ACNET_NO_NODE);
    } else
	ack.setStatus(ACNET_IVM);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleSendRequestWithTimeout(SendRequestWithTimeoutCommand const *cmd, size_t const len)
{
    AckSendRequest ack;

    // In order for the message to be sent, the payload must not be bigger than INTERNAL_ACNET_USER_PACKET (which means the
    // message cannot be bigger than the max payload size plus the command header.)

    if (len >= sizeof(SendRequestWithTimeoutCommand) && len <= INTERNAL_ACNET_USER_PACKET_SIZE + sizeof(SendRequestWithTimeoutCommand)) {
	trunknode_t node = cmd->addr();

	if (node.isBlank())
	    node = taskPool().node();

	if (getAddr(node)) {
	    try {
		RequestPool& reqPool = taskPool().reqPool;

		// Try to allocate a new request ID. If we're successful, send the data to the remote machine.

		ReqInfo* const req = reqPool.alloc(this, cmd->task(), taskPool().node(), node, cmd->flags(), cmd->timeout());
		size_t const msgLen = len - sizeof(SendRequestWithTimeoutCommand);
		AcnetHeader const hdr(ACNET_FLG_REQ | ((cmd->flags() & REQ_M_MULTRPY) ? ACNET_FLG_MLT : 0), ACNET_SUCCESS,
				      node, taskPool().node(), cmd->task(), id(), req->id(),
				      sizeof(AcnetHeader) + MSG_LENGTH(msgLen));

		sendDataToNetwork(hdr, cmd->data(), msgLen);
		++stats.reqXmt;
		++taskPool().stats.reqXmt;
		ack.setRequestId(req->id());
	    } catch (...) {
		ack.setStatus(ACNET_NLM);
		++taskPool().stats.reqQLimit;
	    }
	} else
	    ack.setStatus(ACNET_NO_NODE);
    } else
	ack.setStatus(ACNET_IVM);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleSendReply(SendReplyCommand const *cmd, size_t const len)
{
    AckSendReply ack;

    // In order for the message to be sent, the payload must not be bigger than INTERNAL_ACNET_USER_PACKET (which means the
    // message cannot be bigger than the max payload size plus the command header.)

    if (len >= sizeof(SendReplyCommand) && len <= INTERNAL_ACNET_USER_PACKET_SIZE + sizeof(SendReplyCommand)) {
	status_t const tmp = taskPool().rpyPool.sendReplyToNetwork(this, cmd->rpyid(), cmd->status(), cmd->data(),
								  len - sizeof(SendReplyCommand), cmd->flags() & RPY_M_ENDMULT);

	ack.setStatus(tmp);
    } else
	ack.setStatus(ACNET_IVM);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleIgnoreRequest(IgnoreRequestCommand const *cmd)
{
    Ack ack;

    if (!acceptsRequests())
	ack.setStatus(ACNET_IVM);
    else
	taskPool().rpyPool.endRpyId(cmd->rpyid());

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleRequestAck(RequestAckCommand const *cmd)
{
    Ack ack;

    if (!acceptsRequests())
	ack.setStatus(ACNET_IVM);
    else {
	RpyInfo* const rep = taskPool().rpyPool.rpyInfo(cmd->rpyid());

	if (rep && rep->task().equals(this)) {
	    if (rep->beenAcked() || !decrementPendingRequests())
		ack.setStatus(ACNET_BUG);
	    rep->ackIt();
	} else {
#ifdef DEBUG
	    syslog(LOG_INFO, "ACK REQUEST: Reply id = 0x%04x -- ERROR! Client tried to ACK nonexistent request.", cmd->rpyid().raw());
#endif
	    ack.setStatus(ACNET_NSR);
	}
    }

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleCancel(CancelCommand const *cmd)
{
    Ack ack;

    // Look up the req ID in our table. If the request exists, then we can check to see if the calling task owns the
    // request. If it does, we cancel the requst.

    ReqInfo const* const req = taskPool().reqPool.entry(cmd->reqid());

    if (req && req->task().equals(this)) {
	taskPool().reqPool.cancelReqId(cmd->reqid());
	ack.setStatus(ACNET_SUCCESS);
    } else
	ack.setStatus(ACNET_NSR);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleTaskPid()
{
    AckTaskPid ack;

    ack.setPid(stillAlive() ? pid() : 0);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleNodeStats()
{
    AckNodeStats ack;

    ack.setStats(taskPool().stats);
    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleRenameTask(RenameTaskCommand const *cmd)
{
    AckSendReply ack;

    if (!taskPool().rename(this, cmd->newName()))
	ack.setStatus(ACNET_NAME_IN_USE);

    if (!sendAckToClient(&ack, sizeof(ack)))
	taskPool().removeTask(this);
}

void ExternalTask::handleUnknownCommand(CommandHeader const *cmd, size_t const len)
{
    syslog(LOG_WARNING, "task %s sent unknown command: %04x length:%ld",
		handle().str(), uint16_t(cmd->cmd()), len);

    if (!sendErrorToClient(ACNET_BUG))
	taskPool().removeTask(this);
}

void ExternalTask::handleClientCommand(CommandHeader const* const cmd, size_t const len)
 {
     commandReceived();

     switch (cmd->cmd()) {
      case CommandList::cmdKeepAlive:
         handleKeepAlive();
         break;

      case CommandList::cmdDisconnect:
         handleDisconnect();
         break;

      case CommandList::cmdDisconnectSingle:
         handleDisconnectSingle();
         break;

      case CommandList::cmdSend:
         handleSend((SendCommand const*) cmd, len);
         break;

      case CommandList::cmdReceiveRequests:
         handleReceiveRequests();
         break;

      case CommandList::cmdBlockRequests:
         handleBlockRequests();
         break;

      case CommandList::cmdSendRequest:
         handleSendRequest((SendRequestCommand const*) cmd, len);
         break;

      case CommandList::cmdSendRequestWithTimeout:
         handleSendRequestWithTimeout((SendRequestWithTimeoutCommand const*) cmd, len);
         break;

      case CommandList::cmdSendReply:
         handleSendReply((SendReplyCommand const*) cmd, len);
         break;

      case CommandList::cmdIgnoreRequest:
         handleIgnoreRequest((IgnoreRequestCommand const*) cmd);
         break;

      case CommandList::cmdRequestAck:
         handleRequestAck((RequestAckCommand const*) cmd);
         break;

      case CommandList::cmdCancel:
         handleCancel((CancelCommand const*) cmd);
         break;

      case CommandList::cmdTaskPid:
         handleTaskPid();
         break;

      case CommandList::cmdNodeStats:
         handleNodeStats();
         break;

      case CommandList::cmdRenameTask:
         handleRenameTask((RenameTaskCommand const*) cmd);
         break;

      default:
	handleUnknownCommand(cmd, len);
        break;
    }
}

size_t ExternalTask::totalProp() const
{
    return 3;
}

char const* ExternalTask::propName(size_t idx) const
{
    static char const* const lbl[] = {
	"Command Port",
	"Data Port",
	"Total Socket Errors"
    };

    return (idx < sizeof(lbl) / sizeof(*lbl)) ? lbl[idx] : 0;
}

std::string ExternalTask::propVal(size_t idx) const
{
    std::ostringstream os;

    switch (idx) {
     case 0:
	os << ntohs(saCmd.sin_port);
	return os.str();

     case 1:
	os << ntohs(saData.sin_port);
	return os.str();

     case 2:
	os << totalSocketErrors;
	return os.str();
    }

    return "";
}
