#include <cstdlib>
#include <cassert>
#include <cstdint>
#include "timesensitive.h"


TimeSensitive::TimeSensitive()
{
    lastUpdate = 0;
}

void TimeSensitive::update(Node* const root)
{
    detach();
    lastUpdate = now();

    int64_t const ourExp = expiration();
    Node* current = root->prev();

    while (current != root) {
	if (dynamic_cast<TimeSensitive const*>(current)->expiration() <= ourExp)
	    break;

	current = current->prev();
    }

    current = current->next();

    insertBefore(current);
}
