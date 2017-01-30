#ifndef __LCLTASK_H
#define __LCLTASK_H

#include "exttask.h"

// LocalTask
//
// UDP connections on the local machine
//
class LocalTask : public ExternalTask {
    bool receiving;

    LocalTask();

 protected:
    void handleReceiveRequests();
    void handleBlockRequests();

 public:
    LocalTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t);
    virtual ~LocalTask() {}

    bool acceptsUsm() const { return receiving; }
    bool acceptsRequests() const { return receiving; }

    char const* name() const { return "LocalTask"; }
};

// Local Variables:
// mode:c++
// End:

#endif
