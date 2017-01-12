#include "server.h"

pid_t InternalTask::pid() const
{ 
    return getpid(); 
}

bool InternalTask::equals(TaskInfo const* task) const
{
    InternalTask const* o = dynamic_cast<InternalTask const*>(task);

    return o && (o == this);
}

void InternalTask::sendReplyCore(rpyid_t rpyId, void const* d, uint16_t n,
				    status_t status, uint16_t flags)
{
    RpyInfo const* const rpy = taskPool().rpyPool.rpyInfo(rpyId);

    if (rpy) {
	AcnetHeader const hdr(status.isFatal() ? flags & ~htoas(ACNET_FLG_MLT) : flags,
			    status, rpy->lclNode(), rpy->remNode(),
			    rpy->taskName(), rpy->taskId(), rpy->reqId(),
			    sizeof(AcnetHeader) + MSG_LENGTH(n));
	sendDataToNetwork(hdr, d, n);
	++statRpyXmt;
	++taskPool().statRpyXmt;
    }
}

void InternalTask::sendLastReply(rpyid_t rpy, status_t status,
				    void const* const d, uint16_t n)
{
    sendReplyCore(rpy, d, n, status, ACNET_FLG_RPY);
    taskPool().rpyPool.endRpyId(rpy);
}

void InternalTask::sendReply(rpyid_t rpy, status_t status, void const* d,
				uint16_t n)
{
    sendReplyCore(rpy, d, n, status, ACNET_FLG_RPY | ACNET_FLG_MLT);
}

