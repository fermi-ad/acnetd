#include "server.h"

MulticastTask::MulticastTask(TaskPool& taskPool, taskhandle_t handle, pid_t pid, uint16_t cmdPort,
			     uint16_t dataPort, uint32_t mcAddr) :
    ExternalTask(taskPool, handle, pid, cmdPort, dataPort),  mcAddr(mcAddr)
{
    if (!joinMulticastGroup(sClient, mcAddr))
	throw std::runtime_error("Couldn't join multicast");
}

MulticastTask::~MulticastTask()
{
    dropMulticastGroup(sClient, mcAddr);
}

size_t MulticastTask::totalProp() const
{
    return ExternalTask::totalProp() + 1;
}

char const* MulticastTask::propName(size_t ii) const
{
    if (ii < ExternalTask::totalProp())
	return ExternalTask::propName(ii);

    if (ii == (ExternalTask::totalProp()))
	return "Multicast Address";

    return 0;
}

std::string MulticastTask::propVal(size_t ii) const
{
    if (ii < ExternalTask::totalProp())
	return ExternalTask::propVal(ii);

    if (ii == (ExternalTask::totalProp())) {
	ostringstream os;

	os << (mcAddr >> 24) << "." << (int) ((uint8_t) (mcAddr >> 16)) << "." <<
		(int) ((uint8_t) (mcAddr >> 8)) << "." << (int) ((uint8_t) mcAddr);
	os << " (" << countMulticastGroup(mcAddr) << ")";
	return os.str();
    }

    return "";
}
