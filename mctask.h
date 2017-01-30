#ifndef __MCTASK_H
#define __MCTASK_H

#include "server.h"

// MulticastTask
//
// Client connections for receiving protocol specific
// multicasted messages. Support both local and remote clients
//
class MulticastTask : public ExternalTask {
    ipaddr_t mcAddr;

    void handleReceiveRequests();
    void handleBlockRequests();

    MulticastTask();

 public:
    MulticastTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t, ipaddr_t);
    virtual ~MulticastTask();

    bool acceptsUsm() const { return true; }
    bool acceptsRequests() const { return false; }

    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
    char const* name() const { return "MulticastTask"; }
};

// Local Variables:
// mode:c++
// End:

#endif
