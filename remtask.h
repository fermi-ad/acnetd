#ifndef __REMTASK_H
#define __REMTASK_H

#include "exttask.h"

// RemoteTask
//
// TCP client connections from remote machines
//
class RemoteTask : public ExternalTask {
    bool receiving;
    const ipaddr_t remoteAddr;

    RemoteTask();

    bool rejectTask(taskhandle_t);

protected:
    void handleSendRequest(SendRequestCommand const *, size_t const);
    void handleSendRequestWithTimeout(SendRequestWithTimeoutCommand const*, size_t const);
    void handleSend(SendCommand const *, size_t const);
    void handleReceiveRequests();
    void handleBlockRequests();

 public:
    RemoteTask(TaskPool&, taskhandle_t, taskid_t, pid_t, uint16_t, uint16_t, ipaddr_t);
    virtual ~RemoteTask() {}

    ipaddr_t getRemoteAddr() const { return remoteAddr; }

    bool acceptsUsm() const { return receiving; }
    bool acceptsRequests() const { return receiving; }

    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
    char const* name() const { return "RemoteTask"; }
};

// Local Variables:
// mode:c++
// End:

#endif
