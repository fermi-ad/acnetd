#ifndef __TIMESENSITIVE_H
#define __TIMESENSITIVE_H

#include <time.h>
#include <sys/time.h>
#include "node.h"

// Prototypes of functions (operators) that related to time-keeping.

int64_t now();

// Classes that inherit from this class can be sorted in a "most
// expired" order.

struct TimeSensitive : public Node {
    int64_t lastUpdate;

    TimeSensitive();
    void update(Node*);
    virtual int64_t expiration() const = 0;
};

// Local Variables:
// mode:c++
// End:

#endif
