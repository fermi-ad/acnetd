#include <time.h>
#include <sys/time.h>
#include "node.h"

// Prototypes of functions (operators) that related to time-keeping.

timeval const& now();
timeval operator+(timeval const&, unsigned);
bool operator<=(timeval const&, timeval const&);

// Classes that inherit from this class can be sorted in a "most
// expired" order.

struct TimeSensitive : public Node {
    timeval lastUpdate;

    TimeSensitive();
    void update(Node*);
    virtual timeval expiration() const = 0;
};
