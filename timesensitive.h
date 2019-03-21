#ifndef __TIMESENSITIVE_H
#define __TIMESENSITIVE_H

#include <time.h>
#include <sys/time.h>
#include <inttypes.h>
#include "node.h"

class AbsTime;

class DeltaTime
{
    friend class AbsTime;

    int64_t val;

  public:
    explicit DeltaTime(int64_t const v = 0) : val(v) {}
    DeltaTime(DeltaTime const &o) : val(o.val) {}

    DeltaTime &operator=(DeltaTime const &o)
    {
        val = o.val;
        return *this;
    }

    int64_t get_msec() const { return val; }

    bool operator<(DeltaTime const &o) const { return val < o.val; }
    bool operator>=(DeltaTime const &o) const { return val >= o.val; }
    bool operator!=(DeltaTime const &o) const { return val != o.val; }

    DeltaTime operator-(DeltaTime const &o) const;

    static DeltaTime infinity;
    static DeltaTime noDelay;
};

class AbsTime
{
    uint64_t val;

  public:
    explicit AbsTime(uint64_t const v = 0) : val(v) {}
    AbsTime(AbsTime const &o) : val(o.val) {}

    AbsTime &operator=(AbsTime const &o)
    {
        val = o.val;
        return *this;
    }

    uint64_t get_sec() const { return val / 1000; }

    operator bool() const { return val > 0; }
    bool operator<=(AbsTime const &o) const { return val <= o.val; }
    bool operator<(AbsTime const &o) const { return val < o.val; }

    DeltaTime operator-(AbsTime const &o) const
    {
        return o.val < val ? DeltaTime(val - o.val) : DeltaTime::noDelay;
    }

    AbsTime operator+(DeltaTime const &dt) const
    {
        return AbsTime(val + dt.val);
    }

    AbsTime &operator+=(DeltaTime const &dt)
    {
        val += dt.val;
        return *this;
    }
};

// Prototypes of functions (operators) that related to time-keeping.

AbsTime now();

// Classes that inherit from this class can be sorted in a "most
// expired" order.

template <Node &root>
struct TimeSensitive : public Element<root>
{
    AbsTime lastUpdate;

    TimeSensitive() {}

    void update()
    {
        this->detach();
        lastUpdate = now();

        AbsTime const ourExp = expiration();
        TimeSensitive *current = dynamic_cast<TimeSensitive *>(this->last());

        while (current)
        {
            if (current->expiration() <= ourExp)
            {
                this->insertAfter(current);
                return;
            }
            current = dynamic_cast<TimeSensitive *>(current->prev());
        }
        this->append();
    }

    virtual AbsTime expiration() const = 0;
};

// Local Variables:
// mode:c++
// End:

#endif
