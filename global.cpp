#include "server.h"

const status_t ACNET_SUCCESS(0, 0);
const status_t ACNET_PEND(1, 1);
const status_t ACNET_ENDMULT(1, 2);
const status_t ACNET_NLM(1, -2);
const status_t ACNET_NOREMMEM(1, -3);
const status_t ACNET_TMO(1, -6);
const status_t ACNET_FUL(1, -7);
const status_t ACNET_BUSY(1, -8);
const status_t ACNET_NCN(1, -21);
const status_t ACNET_IVM(1, -23);
const status_t ACNET_NSR(1, -24);
const status_t ACNET_REQREJ(1, -25);
const status_t ACNET_NAME_IN_USE(1, -27);
const status_t ACNET_NCR(1, -28);
const status_t ACNET_NO_NODE(1, -30);
const status_t ACNET_TRP(1, -32);
const status_t ACNET_NOTASK(1, -33);
const status_t ACNET_DISCONNECTED(1, -34);
const status_t ACNET_LEVEL2(1, -35);
const status_t ACNET_NODE_DOWN(1, -42);
const status_t ACNET_BUG(1, -45);
const status_t ACNET_INVARG(1, -50);

// ACNET header creation

AcnetHeader::AcnetHeader() :
    flags_(0), status_(ACNET_SUCCESS.raw()), sTrunk_(0), sNode_(0), cTrunk_(0), cNode_(0),
    svrTaskName_(0), clntTaskId_(0), msgId_(0), msgLen_(0)
{
}

AcnetHeader::AcnetHeader(uint16_t flags, status_t status, trunknode_t svrNode,
			 trunknode_t clntNode, taskhandle_t svrTaskName,
			 uint16_t clntTaskId, uint16_t msgId, uint16_t msgLen) :
    flags_(htoas(flags)), status_(htoas(status.raw())), sTrunk_(svrNode.trunk()),
    sNode_(svrNode.node()), cTrunk_(clntNode.trunk()), cNode_(clntNode.node()),
    svrTaskName_(htoal(svrTaskName.raw())), clntTaskId_(htoas(clntTaskId)),
    msgId_(htoas(msgId)), msgLen_(htoas(msgLen))
{
}

bool AcnetHeader::isEMR()
{
    assert((flags() & ACNET_FLG_TYPE) == ACNET_FLG_RPY);

    return !(flags() & ACNET_FLG_MLT) || status() == ACNET_ENDMULT ||
		status().isFatal();
}

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

// Subtracts two timeval structures and returns the difference in milliseconds. This function assumes that 'a' is greater
// than 'b'.

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

// Local Variables:
// mode:c++
// fill-column:125
// End:
