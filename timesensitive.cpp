#include <limits>
#include "timesensitive.h"

DeltaTime DeltaTime::infinity(std::numeric_limits<int64_t>::max());

DeltaTime DeltaTime::noDelay(0);

// XXX: This operator is still susceptible to overflow problems.
// Delays used for ACNET communications, however, are in the thousands
// of milliseconds, so we shouldn't run into overflow issues.

DeltaTime DeltaTime::operator-(DeltaTime const& o) const
{
    if (*this != infinity)
	return DeltaTime(o != infinity ? val - o.val : 0);
    else
	return infinity;
}
