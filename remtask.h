#ifndef __REMTASK_H
#define __REMTASK_H

#include "exttask.h"

// RemoteTask
//
// TCP client connections from remote machines
//
class RemoteTask : public ExternalTask {
    ipaddr_t remoteAddr;

    RemoteTask();

    bool rejectTask(taskhandle_t);

protected:
    void handleSendRequest(SendRequestCommand const *, size_t const);
    void handleSendRequestWithTimeout(SendRequestWithTimeoutCommand const*, size_t const);
    void handleSend(SendCommand const *, size_t const);

 public:
    RemoteTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t, ipaddr_t);
    virtual ~RemoteTask() {}

    bool acceptsUsm() const { return false; }
    bool acceptsRequests() const { return false; }

    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
    char const* name() const { return "RemoteTask"; }
};

// Local Variables:
// mode:c++
// End:

#endif
