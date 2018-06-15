#ifndef __TIMESENSITIVE_H
#define __TIMESENSITIVE_H

#include <time.h>
#include <sys/time.h>
#include "node.h"

// Prototypes of functions (operators) that related to time-keeping.

int64_t now();

// Classes that inherit from this class can be sorted in a "most
// expired" order.

template <Node& root>
struct TimeSensitive : public Element<root> {
    int64_t lastUpdate;

    TimeSensitive()
    {
	lastUpdate = 0;
    }

    void update()
    {
	this->detach();
	lastUpdate = now();

	int64_t const ourExp = expiration();
	TimeSensitive* current = dynamic_cast<TimeSensitive*>(this->last());

	while (current) {
	    if (current->expiration() <= ourExp) {
		this->insertAfter(current);
		return;
	    }
	    current = dynamic_cast<TimeSensitive*>(current->prev());
	}
	this->append();
    }

    virtual int64_t expiration() const = 0;
};

// Local Variables:
// mode:c++
// End:

#endif
