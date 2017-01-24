#include "server.h"

RemoteTask::RemoteTask(TaskPool& taskPool, taskhandle_t handle, pid_t pid, uint16_t cmdPort,
			     uint16_t dataPort, ipaddr_t remoteAddr) :
    ExternalTask(taskPool, handle, pid, cmdPort, dataPort),  remoteAddr(remoteAddr)
{
}

bool RemoteTask::rejectTask(taskhandle_t task)
{
    if (::rejectTask(task)) {
	syslog(LOG_ERR, "rejecting task %s from %s", task.str(), remoteAddr.str().c_str());
	sendErrorToClient(ACNET_REQREJ);
	return true;
    }

    return false;
}

void RemoteTask::handleSend(SendCommand const *cmd, size_t const len)
{
    if (!rejectTask(cmd->task()))
	ExternalTask::handleSend(cmd, len);
}

void RemoteTask::handleSendRequest(SendRequestCommand const *cmd, size_t const len)
{
    if (!rejectTask(cmd->task()))
	ExternalTask::handleSendRequest(cmd, len);
}

void RemoteTask::handleSendRequestWithTimeout(SendRequestWithTimeoutCommand const* cmd, size_t const len)
{
    if (!rejectTask(cmd->task()))
	ExternalTask::handleSendRequestWithTimeout(cmd, len);
}

size_t RemoteTask::totalProp() const
{
    return ExternalTask::totalProp() + 1;
}

char const* RemoteTask::propName(size_t ii) const
{
    if (ii < ExternalTask::totalProp())
	return ExternalTask::propName(ii);

    if (ii == (ExternalTask::totalProp()))
	return "Remote Address";

    return 0;
}

std::string RemoteTask::propVal(size_t ii) const
{
    if (ii < ExternalTask::totalProp())
	return ExternalTask::propVal(ii);

    if (ii == (ExternalTask::totalProp()))
	remoteAddr.str();

    return "";
}
