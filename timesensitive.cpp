#include <cstdlib>
#include <cassert>
#include <cstdint>
#include "timesensitive.h"

// Determines whether a <= b.

bool operator<=(timeval const& a, timeval const& b)
{
    return (a.tv_sec < b.tv_sec) ||
	((a.tv_sec == b.tv_sec) && (a.tv_usec <= b.tv_usec));
}

// Adds a number of milliseconds to a timeval structure.

timeval operator+(timeval const& t, unsigned ms)
{
    timeval tmp;
    ldiv_t const tmo = ldiv(ms, 1000);
    ldiv_t const adj = ldiv(t.tv_usec + tmo.rem * 1000, 1000000);

    tmp.tv_sec = t.tv_sec + adj.quot + tmo.quot;
    tmp.tv_usec = adj.rem;
    return tmp;
}

// Subtracts two timeval structures and returns the difference in
// milliseconds. This function assumes that 'a' is greater than 'b'.

uint32_t diffInMs(timeval const& a, timeval const& b)
{
    assert(b <= a);
    timeval tmp;

    if (a.tv_usec > b.tv_usec) {
	tmp.tv_sec = a.tv_sec - b.tv_sec;
	tmp.tv_usec = a.tv_usec - b.tv_usec;
    } else {
	tmp.tv_sec = a.tv_sec - b.tv_sec - 1;
	tmp.tv_usec = (1000000 + a.tv_usec) - b.tv_usec;
    }
    return tmp.tv_sec * 1000 + (tmp.tv_usec + 500) / 1000;
}

TimeSensitive::TimeSensitive()
{
    lastUpdate.tv_sec = lastUpdate.tv_usec = 0;
}

void TimeSensitive::update(Node* const root)
{
    detach();
    lastUpdate = now();

    timeval const ourExp = expiration();
    Node* current = root->prev();

    while (current != root) {
	if (dynamic_cast<TimeSensitive const*>(current)->expiration() <= ourExp)
	    break;

	current = current->prev();
    }

    current = current->next();

    insertBefore(current);
}
