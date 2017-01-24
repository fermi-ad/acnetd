#include "server.h"

RemoteTask::RemoteTask(TaskPool& taskPool, taskhandle_t handle, pid_t pid, uint16_t cmdPort,
			     uint16_t dataPort, ipaddr_t remoteAddr) :
    ExternalTask(taskPool, handle, pid, cmdPort, dataPort),  remoteAddr(remoteAddr)
{
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
