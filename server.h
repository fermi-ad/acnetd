#include <poll.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <cassert>
#include <string>
#include <limits>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <memory>
#include <stdexcept>
#include "idpool.h"

class TaskPool;

// These symbols will help us port the code to several Unix operating
// systems that we use. We're trying to keep the conditional code to a
// minimum. All maintainers should try to find the most portable
// solution before resorting to platform-specific solutions.

#define	Linux_Target	1
#define	Darwin_Target	2
#define	NetBSD_Target	3
#define	FreeBSD_Target	4
#define	SunOS_Target	5

#if !defined(THIS_TARGET)
    #error Missing THIS_TARGET definition.
#endif

// Determine platform endianess

#include <inttypes.h>
#if THIS_TARGET == SunOS_Target
    #include <sys/byteorder.h>

    #define BIG_ENDIAN    1234
    #define LITTLE_ENDIAN 4321

    #if defined(_BIG_ENDIAN)
        #define BYTE_ORDER BIG_ENDIAN
    #elif defined(_LITTLE_ENDIAN)
        #define BYTE_ORDER LITTLE_ENDIAN
    #endif
#elif (!defined(BYTE_ORDER) || !defined(BIG_ENDIAN))
    #include <endian.h>
#endif

#if !defined(BYTE_ORDER) || !defined(BIG_ENDIAN)
    #error Missing important endian-defining symbols -- compilation halted.
#endif

#if BYTE_ORDER == BIG_ENDIAN

inline uint16_t htoas(uint16_t v) throw()
{
    return (v >> 8) | (v << 8);
}

inline uint32_t htoal(uint32_t v) throw()
{
    return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}

inline uint16_t atohs(uint16_t v) throw()
{
    return (v >> 8) | (v << 8);
}

inline uint32_t atohl(uint32_t v) throw()
{
    return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
}

#else

#define htoas(v) ((uint16_t) (v))
#define htoal(v) ((uint32_t) (v))
#define atohs(v) ((uint16_t) (v))
#define atohl(v) ((uint32_t) (v))

#endif

#if THIS_TARGET == Darwin_Target
    #define NO_DAEMON 1
#endif

// Project-wide types

typedef uint16_t reqid_t;
typedef uint16_t rpyid_t;
typedef uint8_t acnet_taskid_t;
typedef uint8_t	trunk_t;
typedef uint8_t	node_t;
typedef struct {
    uint16_t t[3];
} __attribute__((packed)) time48_t;

class status_t {
    int16_t s;

 public:
    status_t() : s(0) {}
    status_t(int16_t f, int16_t e) : s(f + e * 256) {}
    explicit status_t(int16_t const sts) : s(sts) {}

    bool operator< (status_t const o) const { return s < o.s; }
    bool operator== (status_t const o) const { return s == o.s; }
    bool operator!= (status_t const o) const { return s != o.s; }

    bool isFatal() const { return s < 0; }
    int16_t raw() const { return s; }
};

class taskhandle_t {
    uint32_t h;

 public:
    taskhandle_t() : h(0) {}
    explicit taskhandle_t(uint32_t const handle) : h(handle) {}

    bool operator< (taskhandle_t const o) const { return h < o.h; }
    bool operator== (taskhandle_t const o) const { return h == o.h; }
    bool operator!= (taskhandle_t const o) const { return h != o.h; }

    bool isBlank() const { return h == 0; }
    uint32_t raw() const { return h; }
};

class nodename_t {
    uint32_t h;

 public:
    nodename_t() : h(0) {}
    explicit nodename_t(uint32_t const handle) : h(handle) {}
    explicit nodename_t(taskhandle_t const o) : h(o.raw()) {}

    bool operator< (nodename_t const o) const { return h < o.h; }
    bool operator== (nodename_t const o) const { return h == o.h; }
    bool operator!= (nodename_t const o) const { return h != o.h; }

    bool isBlank() const { return h == 0; }
    uint32_t raw() const { return h; }
};

#include "trunknode.h"

// Acnet header macros

#define	ACNET_PORT		(6801)		// Standard ACNET port number (UDP)
#define ACNET_CLIENT_PORT	(ACNET_PORT + 1)
#define UTI_VERSION		(0x0800)

#define	REPLY_DELAY		5u
#define	REQUEST_TIMEOUT		390u

#ifdef NO_SWAP
#define MSG_LENGTH(s)		(size_t)(s)
#else
#define MSG_LENGTH(s)		(size_t)((s) + (s) % 2)
#endif

#define	ACNET_FLG_USM	(0x0)
#define	ACNET_FLG_REQ	(0x2)
#define	ACNET_FLG_RPY	(0x4)
#define	ACNET_FLG_CAN	(0x200 | ACNET_FLG_USM)
#define	ACNET_FLG_TYPE	(ACNET_FLG_USM | ACNET_FLG_REQ | ACNET_FLG_RPY)

#define	ACNET_FLG_MLT	(0x1)
#define	ACNET_FLG_CHK	(0x400)
#define	ACNET_FLG_NBW	(0x100)

#define PKT_TYPE(f)	   ((f) & ACNET_FLG_TYPE)
#define PKT_USM_FLDS(f)	   ((f) & (ACNET_FLG_TYPE | ACNET_FLG_CAN))
#define PKT_IS_REPLY(f)	   (PKT_TYPE(f) == ACNET_FLG_RPY)
#define PKT_IS_REQUEST(f)  (PKT_TYPE(f) == ACNET_FLG_REQ)
#define PKT_IS_USM(f)	   (PKT_USM_FLDS(f) == ACNET_FLG_USM)
#define PKT_IS_CANCEL(f)   (PKT_USM_FLDS(f) == ACNET_FLG_CAN)

#define	ACNET_MIN_TRUNK		(9)		// minimum IP trunk number
#define	ACNET_MAX_TRUNK		(14)		// maximum IP trunk number

#define ACNET_MULTICAST trunknode_t(255)	// Multicast to all nodes

#define	RPY_M_ENDMULT		(0x02)		// terminate multiple reply request
#define	REQ_M_MULTRPY		(0x01)		// multiple reply request

#define N_REQID			4096
#define N_RPYID			4096

// ACNET protocol packet header

class AcnetHeader {
    uint16_t flags_;
    int16_t status_;
    uint8_t sTrunk_;
    uint8_t sNode_;
    uint8_t cTrunk_;
    uint8_t cNode_;
    uint32_t svrTaskName_;
    uint16_t clntTaskId_;
    uint16_t msgId_;
    uint16_t msgLen_;
    uint8_t msg_[];

 public:
    AcnetHeader();
    AcnetHeader(uint16_t, status_t, trunknode_t, trunknode_t, taskhandle_t, uint16_t, uint16_t, uint16_t);
    uint16_t flags() const { return atohs(flags_); }
    status_t status() const { return status_t(atohs(status_)); }
    trunknode_t client() const { return trunknode_t(cTrunk_, cNode_); }
    trunknode_t server() const { return trunknode_t(sTrunk_, sNode_); }
    taskhandle_t svrTaskName() const { return taskhandle_t(atohl(svrTaskName_)); }
    uint16_t clntTaskId() const { return atohs(clntTaskId_); }
    uint16_t msgId() const { return atohs(msgId_); }
    uint16_t msgLen() const { return atohs(msgLen_); }
    uint8_t const *msg() const { return msg_; }

    void setStatus(status_t status) { status_ = htoas(status.raw()); }
    void setFlags(uint16_t flags) { flags_ = htoas(flags); }
    void setClient(trunknode_t tn) { cTrunk_ = tn.trunk(); cNode_ = tn.node(); }
    bool isEMR();
} __attribute((packed));

#define INTERNAL_ACNET_PACKET_SIZE	int(65534 - sizeof(ip) - sizeof(udphdr))
#define	INTERNAL_ACNET_USER_PACKET_SIZE	(INTERNAL_ACNET_PACKET_SIZE - sizeof(AcnetHeader))

extern const status_t ACNET_ENDMULT;
extern const status_t ACNET_PEND;
extern const status_t ACNET_SUCCESS;
extern const status_t ACNET_NLM;
extern const status_t ACNET_NOREMMEM;
extern const status_t ACNET_TMO;
extern const status_t ACNET_FUL;
extern const status_t ACNET_BUSY;
extern const status_t ACNET_NCN;
extern const status_t ACNET_IVM;
extern const status_t ACNET_NSR;
extern const status_t ACNET_NAME_IN_USE;
extern const status_t ACNET_NCR;
extern const status_t ACNET_NO_NODE;
extern const status_t ACNET_TRP;
extern const status_t ACNET_NOTASK;
extern const status_t ACNET_DISCONNECTED;
extern const status_t ACNET_LEVEL2;
extern const status_t ACNET_NODE_DOWN;
extern const status_t ACNET_BUG;
extern const status_t ACNET_INVARG;

// This section starts a hierarchy of classes that describe the layout
// of commands passed between the acnet task and the clients. We use
// inheritance rather than nested structures to clean up the syntax
// when referring to nested fields. Another benefit from inheritance
// is that we can use the template mechanisms of the language to stuff
// the 'cmd' field so it is always correct. Finally, inheritance
// allows us to implicitly upcast to CommandHeader*, which further
// cleans up the syntax (one of the ugly things about socket
// programming is the constantly having to typecast sockaddr_un or
// sockaddr_in pointers to sockaddr.)

enum class CommandList : uint16_t {
        cmdKeepAlive	 	= 0,

	cmdConnect    		= 1,
	cmdRenameTask 		= 2,
	cmdDisconnect		= 3,

	cmdSend 		= 4,
	cmdSendRequest 		= 5,
	cmdReceiveRequests	= 6,
	cmdSendReply		= 7,
	cmdCancel 		= 8,
	cmdRequestAck 		= 9,

	cmdAddNode 		= 10,
	cmdNameLookup 		= 11,
	cmdNodeLookup 		= 12,
	cmdLocalNode 		= 13,

	cmdTaskPid		= 14,
	cmdGlobalStats		= 15,
	cmdAckGlobalStats	= 16,

	cmdDisconnectSingle	= 17,

	cmdSendRequestWithTmo	= 18,
	cmdIgnoreRequest        = 19,
	cmdBlockRequests	= 20,

	cmdTcpConnect  		= 21,

	cmdDefaultNode 		= 22
};

enum class AckList : uint16_t {
	ackAck			= 0,

	ackConnect		= 1,

	ackSendRequest		= 2,
	ackSendReply		= 3,

	ackNameLookup		= 4,
	ackNodeLookup		= 5,

	ackTaskPid		= 6,
	ackGlobalStats		= 7,
};

// This is the command header for all commands send from the client to
// the acnet task.

struct CommandHeader {
    uint16_t const cmd;
    uint32_t clientName;
    uint32_t virtualNodeName;

 private:
    CommandHeader();
    CommandHeader(CommandHeader const&);
    CommandHeader& operator=(CommandHeader const&);

 public:
    CommandHeader(CommandList Cmd) : cmd(htons(Cmd)), clientName(0),
				     virtualNodeName(0) { }
} __attribute__((packed));

template<CommandList Cmd>
struct CommandHeaderBase : public CommandHeader {
    CommandHeaderBase() : CommandHeader(Cmd) { }
} __attribute__((packed));

// Sent by a client when it wants to connect to the network. An
// AckConnect is sent back to the client.

struct ConnectCommand : public CommandHeaderBase<CommandList::cmdConnect> {
    pid_t pid;
    uint16_t dataPort;
} __attribute__((packed));

struct TcpConnectCommand : public ConnectCommand {
    uint32_t remoteAddr;
} __attribute__((packed));

// Sent by a client periodicly to keep it's Acnet connection.  An
// AckCommand is sent back to the client.

struct KeepAliveCommand : public CommandHeaderBase<CommandList::cmdKeepAlive> {
} __attribute__((packed));

// Sent by a client when it wants to rename a connected task. An
// AckConnect is sent back to the client.

struct RenameTaskCommand :
    public CommandHeaderBase<CommandList::cmdRenameTask> {
    uint32_t newName;
} __attribute__((packed));

// Sent by a client when it wants to disconnect from the network. An
// AckCommand is sent back to the client.

struct DisconnectCommand :
    public CommandHeaderBase<CommandList::cmdDisconnect> {
} __attribute__((packed));

struct DisconnectSingleCommand :
    public CommandHeaderBase<CommandList::cmdDisconnectSingle> {
} __attribute__((packed));

// Sent by a client wanting to send an USM. An AckCommand is sent to
// the client.

struct SendCommand : public CommandHeaderBase<CommandList::cmdSend> {
    uint32_t taskName;
    uint16_t addr;
    uint8_t msg[];
} __attribute__((packed));

// Sent by a client that wants to be a "RUM listener". The acnet task
// marks the task handle (previously registered with a ConnectCommand)
// as able to receive.

struct ReceiveRequestCommand :
    public CommandHeaderBase<CommandList::cmdReceiveRequests> {
} __attribute__((packed));

// Sent by a client that wants to do a lookup of a node name to its
// trunk/node combo.

struct NameLookupCommand :
    public CommandHeaderBase<CommandList::cmdNameLookup> {
    uint32_t name;
} __attribute__((packed));

// Sent by a client that wants to do a lookup of a node name from its
// trunk/node combo.

struct NodeLookupCommand :
    public CommandHeaderBase<CommandList::cmdNodeLookup> {
    uint16_t addr;
} __attribute__((packed));

struct LocalNodeCommand :
    public CommandHeaderBase<CommandList::cmdLocalNode> {
} __attribute__((packed));

struct DefaultNodeCommand :
    public CommandHeaderBase<CommandList::cmdDefaultNode> {
} __attribute__((packed));

struct SendRequestCommand :
    public CommandHeaderBase<CommandList::cmdSendRequest> {
    uint32_t task;
    uint16_t addr;
    uint16_t flags;
    uint8_t data[];
} __attribute__((packed));

struct SendRequestWithTmoCommand :
    public CommandHeaderBase<CommandList::cmdSendRequestWithTmo> {
    uint32_t task;
    uint16_t addr;
    uint16_t flags;
    uint32_t tmo;
    uint8_t data[];
} __attribute__((packed));

struct SendReplyCommand : public CommandHeaderBase<CommandList::cmdSendReply> {
    rpyid_t rpyid;
    uint16_t flags;
    int16_t status;
    uint8_t data[];
} __attribute__((packed));

struct IgnoreRequestCommand :
    public CommandHeaderBase<CommandList::cmdIgnoreRequest> {
    rpyid_t rpyid;
} __attribute__((packed));

struct CancelCommand : public CommandHeaderBase<CommandList::cmdCancel> {
    reqid_t reqid;
} __attribute__((packed));

struct BlockRequestCommand :
    public CommandHeaderBase<CommandList::cmdBlockRequests> {
} __attribute__((packed));

struct RequestAckCommand :
    public CommandHeaderBase<CommandList::cmdRequestAck> {
    rpyid_t rpyid;
} __attribute__((packed));

struct AddNodeCommand : public CommandHeaderBase<CommandList::cmdAddNode> {
    uint32_t ipAddr;
    uint32_t optFlgs;
    uint16_t addr;
    uint32_t nodeName;
} __attribute__((packed));

struct TaskPidCommand : public CommandHeaderBase<CommandList::cmdTaskPid> {
    uint32_t task;
} __attribute__((packed));

struct GlobalStats {
    uint32_t statUsmRcv;
    uint32_t statReqRcv;
    uint32_t statRpyRcv;
    uint32_t statUsmXmt;
    uint32_t statReqXmt;
    uint32_t statRpyXmt;
    uint32_t statReqQLimit;
} __attribute__((packed));

struct GlobalStatsCommand :
    public CommandHeaderBase<CommandList::cmdGlobalStats> {
} __attribute__((packed));


class AckHeader {
    uint16_t const cmd_;
    int16_t status_;

    AckHeader();

 public:
    AckHeader(AckList cmd) : cmd_(htons(cmd))
    { setStatus(ACNET_SUCCESS); }
    AckList cmd() const { return AckList(ntohs(cmd_)); }
    status_t status() const { return status_t(ntohs(status_)); }
    void setStatus(status_t status) { status_ = htons(status.raw()); }
} __attribute__((packed));

// This command is only sent from the acnet task to the clients to
// acknowledge a command. This class contains a status field to pass
// the success or failure codes back to the client.

struct Ack : public AckHeader {
    Ack() : AckHeader(AckList::ackAck) { }
} __attribute__((packed));

struct AckConnect : public AckHeader {
    AckConnect() : AckHeader(AckList::ackConnect) { }
    acnet_taskid_t id;
    uint32_t clientName;
} __attribute__((packed));

struct AckSendRequest : public AckHeader {
    AckSendRequest() : AckHeader(AckList::ackSendRequest) { }
    reqid_t reqid;
} __attribute__((packed));

struct AckSendReply : public AckHeader {
    AckSendReply() : AckHeader(AckList::ackSendReply) { }
    uint16_t flags;
} __attribute__((packed));

struct AckNameLookup : public AckHeader {
    AckNameLookup() : AckHeader(AckList::ackNameLookup) { }
    trunk_t trunk;
    node_t node;
} __attribute__((packed));

struct AckNodeLookup : public AckHeader {
    AckNodeLookup() : AckHeader(AckList::ackNodeLookup) { }
    uint32_t name;
} __attribute__((packed));

struct AckTaskPid : public AckHeader {
    AckTaskPid() : AckHeader(AckList::ackTaskPid) { }
    pid_t pid;
} __attribute__((packed));

struct AckGlobalStats : public AckHeader {
    AckGlobalStats() : AckHeader(AckList::ackGlobalStats) { }
    GlobalStats	stats;
} __attribute__((packed));

// Asynchronous message passing to client processes

struct AcnetClientMessage {
    enum {
	Ping,
	DumpProcessIncomingPacketsOn,
	DumpProcessIncomingPacketsOff,
	DumpTaskIncomingPacketsOn,
	DumpTaskIncomingPacketsOff,
    };

    pid_t pid;
    uint32_t task;
    uint8_t type;
    union args {
    } args;

    AcnetClientMessage() : pid(0), task(0), type(Ping) {}
    AcnetClientMessage(taskhandle_t task, uint8_t type) : task(task.raw()), type(type) {}
} __attribute__((packed));

// Project-wide types...

class StatCounter {
    uint32_t counter;

 public:
    inline explicit StatCounter(uint32_t initialValue = 0)
    {
	counter = initialValue;
    }

    inline StatCounter& operator++()
    {
	if (counter < std::numeric_limits<uint32_t>::max())
	    ++counter;

	return *this;
    }

    inline StatCounter& operator+=(StatCounter const& c)
    {
	uint32_t tmp = counter + c.counter;
	counter = tmp < counter ? (uint32_t) std::numeric_limits<uint32_t>::max() : tmp;
	return *this;
    }

    inline void reset()
    {
	counter = 0;
    }

    inline operator uint16_t() const
    {
	return std::min(counter, (uint32_t) std::numeric_limits<uint16_t>::max());
    }

    inline operator uint32_t() const
    {
	return counter;
    }
};

class TaskInfo;

struct reqDetail {
    uint16_t id;
    uint16_t remNode;
    uint32_t remName;
    uint32_t lclName;
    uint32_t initTime;
    uint32_t lastUpdate;
};

struct AcnetReqList {
    uint16_t total;
    reqid_t ids[N_REQID];
};

struct AcnetRpyList {
    uint16_t total;
    rpyid_t ids[N_RPYID];
};

#include "timesensitive.h"

class RequestPool;

// This class encompasses all the information related to request ids.

class ReqInfo : public TimeSensitive {
 friend class IdPool<ReqInfo, reqid_t, N_REQID>;
 friend class RequestPool;

 private:
    ReqInfo() : task_(0), flags(0), tmoMs(0), initTime_(0) {}

    TaskInfo* task_;
    taskhandle_t taskName_;
    trunknode_t lclNode_;
    trunknode_t remNode_;
    uint16_t flags;
    uint32_t tmoMs;
    bool mcast;
    time_t initTime_;

public:
    mutable StatCounter totalPackets;

    reqid_t id() const;
    TaskInfo& task() const		{ return *task_; }
    void bumpPktStats() const		{ ++totalPackets; }
    bool wantsMultReplies() const	{ return flags & ACNET_FLG_MLT; }
    bool multicasted() const		{ return mcast; }
    timeval expiration() const		{ return lastUpdate + tmoMs; }
    taskhandle_t taskName() const	{ return taskName_; }
    trunknode_t lclNode() const		{ return lclNode_; }
    trunknode_t remNode() const		{ return remNode_; }
    time_t initTime() const		{ return initTime_; }
};

class RequestPool {
 friend class ReqInfo;

 private:
    IdPool<ReqInfo, reqid_t, N_REQID> idPool;
    Node root;

 public:
    ReqInfo *alloc(TaskInfo*, taskhandle_t, trunknode_t, trunknode_t, uint16_t, uint32_t);
    void release(ReqInfo *);

    bool cancelReqId(reqid_t, bool = true, bool = false);
    void cancelReqToNode(trunknode_t const);
    int sendRequestTimeoutsAndGetNextTimeout();

    ReqInfo *next(ReqInfo const * const req) const 	{ return idPool.next(req); }
    ReqInfo *entry(reqid_t const id) 			{ return idPool.entry(id); }

    ReqInfo *oldest();
    void update(ReqInfo* req) 				{ req->update(&root); }

    void fillActiveRequests(AcnetReqList&l, uint8_t, uint16_t const*, uint16_t);
    bool fillRequestDetail(reqid_t, reqDetail* const);
    void generateReqReport(std::ostream&);
};

struct rpyDetail {
    uint16_t id;
    uint16_t reqId;
    uint16_t remNode;
    uint32_t remName;
    uint32_t lclName;
    uint32_t initTime;
    uint32_t lastUpdate;
};

class ReplyPool;

// This class encompasses all the information related to reply ids.

class RpyInfo : public TimeSensitive {
 friend class IdPool<RpyInfo, rpyid_t, N_RPYID>;
 friend class ReplyPool;

 private:
    RpyInfo() : task_(0), flags(0), taskId_(0), initTime_(0) {}

    TaskInfo* task_;
    uint16_t flags;
    trunknode_t lclNode_;
    trunknode_t remNode_;
    taskhandle_t taskName_;
    acnet_taskid_t taskId_;
    bool mcast;
    reqid_t reqId_;
    time_t initTime_;
    bool acked;

 public:
    mutable StatCounter totalPackets;

    void ackIt()
    {
#ifdef DEBUG
	syslog(LOG_INFO, "ACK REQUEST: id = 0x%04x", reqId());
#endif
	acked = true;
    }

    rpyid_t id() const;
    TaskInfo& task() const		{ return *task_; }
    void bumpPktStats() const		{ ++totalPackets; }
    bool beenAcked() const		{ return acked; }
    bool isMultReplier() const		{ return flags & ACNET_FLG_MLT; }
    bool multicasted() const		{ return mcast; }
    timeval expiration() const 		{ return lastUpdate + REPLY_DELAY * 1000; }
    taskhandle_t taskName() const	{ return taskName_; }
    acnet_taskid_t taskId() const	{ return taskId_; }
    trunknode_t lclNode() const		{ return lclNode_; }
    trunknode_t remNode() const		{ return remNode_; }
    reqid_t reqId() const		{ return reqId_; }
    time_t initTime() const		{ return initTime_; }

    bool xmitReply(status_t, void const*, size_t, bool);
};

class ReplyPool {
 friend class RpyInfo;

    #ifndef NO_PINGER
    typedef std::map<trunknode_t, unsigned> ActiveTargetMap;

    class Pinger {
	timeval expires;
	ActiveTargetMap const& map;
	ActiveTargetMap::const_iterator current;

     public:
	Pinger(ActiveTargetMap const& m) :
	    expires(now()), map(m), current(m.end())
	{
	}

	void invalidate()
	{
	    if (current != map.end())
		current = map.end();
	}
    };
    #endif

 private:
    typedef std::multimap<uint32_t, RpyInfo*> ActiveMap;
    typedef std::pair<ActiveMap::iterator, ActiveMap::iterator> ActiveRangeIterator;

    IdPool<RpyInfo, rpyid_t, N_REQID> idPool;
    Node root;

    ActiveMap activeMap;

    #ifndef NO_PINGER
    ActiveTargetMap targetMap;
    Pinger pinger;
    #endif

 public:
    ReplyPool() : pinger(targetMap) {};

    RpyInfo *alloc(TaskInfo *, reqid_t, acnet_taskid_t, taskhandle_t, trunknode_t, trunknode_t, uint16_t);
    void release(RpyInfo *);

    RpyInfo* rpyInfo(rpyid_t);
    RpyInfo* rpyInfo(trunknode_t, reqid_t);

    RpyInfo *next(RpyInfo const * const rpy) const 	{ return idPool.next(rpy); }
    RpyInfo *entry(reqid_t const id) 			{ return idPool.entry(id); }

    status_t sendReplyToNetwork(TaskInfo const*, rpyid_t, status_t, void const*, size_t, bool);
    void endRpyToNode(trunknode_t const);
    void endRpyId(rpyid_t, status_t = ACNET_SUCCESS);
    int sendReplyPendsAndGetNextTimeout();

    RpyInfo *getOldest();
    void update(RpyInfo* rpy) 		{ rpy->update(&root); }

    void fillActiveReplies(AcnetRpyList&, uint8_t, uint16_t const*, uint16_t);
    bool fillReplyDetail(rpyid_t, rpyDetail* const);
    void generateRpyReport(std::ostream&);
};

// Classes derived from this class cannot be copied.

class Noncopyable {
    Noncopyable(Noncopyable const&);
    Noncopyable& operator=(Noncopyable const&);

 public:
    Noncopyable() {}
    ~Noncopyable() {}
};

typedef std::set<reqid_t> ReqList;
typedef std::set<rpyid_t> RpyList;

#define	MAX_TASKS		(256)

// TaskInfo
//
// Information related to all connected  tasks.

class TaskInfo : private Noncopyable {
    time_t boot;
    TaskPool& taskPool_;
    taskhandle_t handle_;
    acnet_taskid_t id_;

    static unsigned const maxPendingRequestsAccepted = 256;

    unsigned pendingRequests;
    unsigned maxPendingRequests;

 protected:
    ReqList requests;
    RpyList replies;

 public:
    friend class TaskPool;

    mutable StatCounter statUsmRcv;
    mutable StatCounter statReqRcv;
    mutable StatCounter statRpyRcv;
    mutable StatCounter statUsmXmt;
    mutable StatCounter statReqXmt;
    mutable StatCounter statRpyXmt;
    mutable StatCounter statLostPkt;

    TaskInfo(TaskPool&, taskhandle_t);
    virtual ~TaskInfo() {}

    void setHandle(taskhandle_t th)		{ handle_ = th; }

    // Informational

    time_t connectedTime() const;
    virtual pid_t pid() const = 0;
    virtual bool acceptsUsm() const = 0;
    virtual bool acceptsRequests() const = 0;
    virtual bool isPromiscuous() const  = 0;
    virtual bool needsToBeThrottled() const = 0;
    virtual bool stillAlive(int = 0) const = 0;
    virtual bool equals(TaskInfo const* o) const = 0;
    TaskPool& taskPool() const	{ return taskPool_; }
    taskhandle_t handle() const	{ return handle_; }
    acnet_taskid_t id() const 	{ return id_; }
    bool isReceiving() { return acceptsUsm() || acceptsRequests(); }

    // Receiving data from network
    
    virtual bool sendDataToClient(AcnetHeader const*) = 0;
    virtual bool sendMessageToClient(AcnetClientMessage*) = 0;

    // Request/reply 
    
    bool addReply(rpyid_t id)			{ return replies.insert(id).second; }
    bool addRequest(reqid_t id)			{ return requests.insert(id).second; }
    size_t requestCount() const			{ return requests.size(); }
    size_t replyCount() const			{ return replies.size(); }
    bool removeReply(rpyid_t id)		{ return replies.erase(id) != 0; }
    bool removeRequest(reqid_t id)		{ return requests.erase(id) != 0; }
    bool decrementPendingRequests();
    bool testPendingRequestsAndIncrement();

    // ACNET report
    
    virtual char const* name() const = 0;
    virtual size_t totalProp() const = 0;
    virtual char const* propName(size_t) const = 0;
    virtual std::string propVal(size_t) const = 0;
#ifndef NO_REPORT
    void report(std::ostream&) const;
#endif
};


// ExternalTask
//
// Basis for all task connections external from acnetd
//
class ExternalTask : public TaskInfo {
    pid_t const pid_;
    sockaddr_in saCmd, saData;
    int contSocketErrors;
    uint32_t totalSocketErrors;
    mutable time_t lastCommandTime, lastAliveCheckTime;

    ExternalTask();

 protected:
    bool checkResult(ssize_t);

    // Client command handlers
    
    virtual void handleCancel(CancelCommand const *);
    virtual void handleDisconnect();
    virtual void handleDisconnectSingle();
    virtual void handleReceiveRequests();
    virtual void handleBlockRequests();
    virtual void handleRenameTask(RenameTaskCommand const *);
    virtual void handleRequestAck(RequestAckCommand const *);
    virtual void handleSendReply(SendReplyCommand const *, size_t const);
    virtual void handleIgnoreRequest(IgnoreRequestCommand const *);
    virtual void handleSendRequest(SendRequestCommand const *, size_t const);
    virtual void handleSendRequestWithTmo(SendRequestWithTmoCommand const*, size_t const);
    virtual void handleSend(SendCommand const *, size_t const);
    virtual void handleTaskPid();
    virtual void handleGlobalStats();
    virtual void handleKeepAlive();
    virtual void handleUnknownCommand(CommandHeader const *, size_t len);

 public:
    ExternalTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t);
    virtual ~ExternalTask() {}

    bool stillAlive(int = 0) const;
    bool isPromiscuous() const { return false; }

    pid_t pid() const { return pid_; }
    uint16_t commandPort() const { return ntohs(saCmd.sin_port); }
    uint16_t dataPort() const { return ntohs(saData.sin_port); }

    void handleClientCommand(CommandHeader const* const, size_t const);
    bool sendErrorToClient(status_t);

    bool equals(TaskInfo const*) const;
    bool needsToBeThrottled() const { return true; }
    void commandReceived() const { lastCommandTime = now().tv_sec; }

    ssize_t sendAckToClient(TaskInfo *, void const*, size_t);
    bool sendAckToClient(void const*, size_t);
    bool sendDataToClient(AcnetHeader const*);
    bool sendMessageToClient(AcnetClientMessage*);

    char const* name() const { return "ExternalTask"; }
    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
};

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

// RemoteTask
//
// TCP client connections from remote machines
//
class RemoteTask : public ExternalTask {
    uint32_t remoteAddr;

    RemoteTask();

 public:
    RemoteTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t, uint32_t);
    virtual ~RemoteTask() {}
    
    bool acceptsUsm() const { return false; }
    bool acceptsRequests() const { return false; }

    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
    char const* name() const { return "RemoteTask"; }
};

// MulticastTask
//
// Client connections for receiving protocol specific
// multicasted messages. Support both local and remote clients
//
class MulticastTask : public ExternalTask {
    uint32_t mcAddr;

    MulticastTask();

 public:
    MulticastTask(TaskPool&, taskhandle_t, pid_t, uint16_t, uint16_t, uint32_t);
    virtual ~MulticastTask();

    bool acceptsUsm() const { return true; }
    bool acceptsRequests() const { return false; }

    size_t totalProp() const;
    char const* propName(size_t) const;
    std::string propVal(size_t) const;
    char const* name() const { return "MulticastTask"; }
};

// InternalTask
//
// Basis for all tasks connection from within acnetd
//
class InternalTask : public TaskInfo {
    void sendReplyCore(rpyid_t, void const*, uint16_t, status_t, uint16_t);

 protected:
    void _clear_receiving() { }

 public:
    InternalTask(TaskPool& taskPool, taskhandle_t handle) :
	TaskInfo(taskPool, handle) { }
    virtual ~InternalTask() {}

    pid_t pid() const;
    bool acceptsUsm() const { return true; }
    bool acceptsRequests() const { return true; }
    bool needsToBeThrottled() const { return false; }
    void commandReceived() const { }
    bool stillAlive(int) const { return true; }

    bool equals(TaskInfo const*) const;
    bool isPromiscuous() const { return false; }
    bool sendAckToClient(void const*, size_t) { return 0; }
    bool sendMessageToClient(AcnetClientMessage*) { return 0; }
    ssize_t sendAckToClient(TaskInfo *, void const*, size_t);

    char const* name() const { return "InternalTask"; }
    size_t totalProp() const { return 0; }
    char const* propName(size_t) const { return 0; }
    std::string propVal(size_t) const { return ""; }

    void sendReply(rpyid_t, status_t, void const*, uint16_t);

    void sendReply(rpyid_t rid, status_t s)
    {
	sendReply(rid, s, 0, 0);
    }

    void sendLastReply(rpyid_t, status_t, void const*, uint16_t);

    void sendLastReply(rpyid_t rid, status_t s)
    {
	sendLastReply(rid, s, 0, 0);
    }
};

// AcnetTask
//
// Connection to handle all ACNET level information requests
//
class AcnetTask : public InternalTask {

    // Various typecode handlers

    void versionHandler(rpyid_t);
    void activeReplies(rpyid_t, uint8_t, uint16_t const* const, uint16_t);
    void packetCountHandler(rpyid_t);
    void taskIdHandler(rpyid_t, uint16_t const* const, uint16_t);
    void taskNameHandler(rpyid_t, uint8_t);
    void killerMessageHandler(rpyid_t, uint8_t, uint16_t const* const, uint16_t);
    void tasksHandler(rpyid_t, uint8_t);
    void pingHandler(rpyid_t);
    void taskResourcesHandler(rpyid_t);
    void resetStats();
    void nodeStatsHandler(rpyid_t, uint8_t);
    void tasksStatsHandler(rpyid_t, uint8_t);
    void ipNodeTableHandler(rpyid_t, uint8_t, const uint16_t*, uint16_t);
    void timeHandler(rpyid_t, uint8_t);
    void debugHandler(rpyid_t, uint8_t, const uint16_t*, uint16_t);
    void activeRequests(rpyid_t, uint8_t, const uint16_t*, uint16_t);
    void replyDetail(rpyid_t, const uint16_t*, uint16_t);
    void requestDetail(rpyid_t, const uint16_t*, uint16_t);
    void requestReport(rpyid_t);

 public:
    AcnetTask(TaskPool&);
    virtual ~AcnetTask() {}

    char const* name() const { return "AcnetTask"; }
    bool isPromiscuous() const { return true; }
    bool sendDataToClient(AcnetHeader const*);
    bool sendMessageToClients(AcnetClientMessage*) const;

};

typedef std::multimap<taskhandle_t, TaskInfo*> TaskHandleMap;
typedef std::pair<TaskHandleMap::const_iterator, TaskHandleMap::const_iterator> TaskRangeIterator;
typedef std::vector<TaskInfo*> TaskList;

// TaskPool
//
// This class holds the entire state of an ACNET node allowing acnetd
// to become a host for multiple "virtual" acnetd nodes
//
class TaskPool : private Noncopyable {
    time_t taskStatTimeBase;
    trunknode_t node_;
    nodename_t nodeName_;
    TaskInfo *tasks_[MAX_TASKS];
    TaskHandleMap active;
    TaskList removed;

    int nextFreeTaskId();
    void removeInactiveTasks();

 public:
    RequestPool reqPool;
    ReplyPool rpyPool;

    mutable StatCounter statUsmRcv;
    mutable StatCounter statReqRcv;
    mutable StatCounter statRpyRcv;
    mutable StatCounter statUsmXmt;
    mutable StatCounter statReqXmt;
    mutable StatCounter statRpyXmt;
    mutable StatCounter statReqQLimit;

    TaskPool(trunknode_t, nodename_t);
    ~TaskPool() {}

    void handleConnect(sockaddr_in const&, ConnectCommand const* const, size_t);
    void addTask(TaskInfo *);

    trunknode_t node() const { return node_; }
    nodename_t nodeName() const { return nodeName_; }

    size_t fillBufferWithTaskInfo(uint8_t, uint16_t[]);
    size_t fillBufferWithTaskStats(uint8_t, void*);

#ifndef NO_REPORT
    void generateReport();
    void generateNodeDataReport(std::ostream& os);
    void generateTaskReport(std::ostream&) const;
#endif

    bool full();
    size_t requestCount() const;
    size_t replyCount() const;
    size_t activeCount() const;
    size_t rumHandleCount() const;
    TaskInfo* getTask(acnet_taskid_t) const;
    TaskInfo* getTask(taskhandle_t, uint16_t) const;
    bool taskExists(taskhandle_t) const;
    TaskRangeIterator tasks(taskhandle_t) const;
    void removeAllTasks();
    void removeTask(TaskInfo *);
    void removeOnlyThisTask(TaskInfo *, status_t = ACNET_DISCONNECTED, bool = false);
    bool rename(TaskInfo *, taskhandle_t);
    bool isPromiscuous(taskhandle_t) const;
};

typedef std::map<nodename_t, TaskPool *> TaskPoolMap;

extern uint16_t acnetPort;
class DataOut;
struct pollfd;

class IpInfo : private Noncopyable {
    sockaddr_in in;
    nodename_t name_;
    DataOut* partial;

    IpInfo& operator= (IpInfo const&);

 public:
    IpInfo();
    IpInfo(nodename_t, uint32_t, uint16_t = acnetPort);

    sockaddr_in const* addr() const { return &in; }
    DataOut* partialBuffer() const { return partial; }
    void setPartialBuffer(DataOut* ptr) { partial = ptr; }
    bool matches(nodename_t nm) const { return name_ == nm; }
    nodename_t name() const { return name_; }
    void update(nodename_t n, uint32_t a)
    {
	if (!n.isBlank())
	    name_ = n;

	if (a) {
	    in.sin_addr.s_addr = htonl(a);
	    in.sin_port = htons(acnetPort);
	}
    }
};

#define TCP_CLIENT_PING	(0)
#define ACNETD_COMMAND	(1)
#define ACNETD_ACK	(2)
#define ACNETD_DATA	(3)

class TcpClientProtocolHandler : private Noncopyable
{
    int getSocketPort(int);

 protected:
    int sTcp, sCmd, sData;
    nodename_t tcpNode;
    uint32_t remoteAddr;

    bool readBytes(void *, size_t);
    bool handleClientCommand(CommandHeader *, size_t);

 public:
    TcpClientProtocolHandler(int, int, int, nodename_t, uint32_t);
    virtual ~TcpClientProtocolHandler() {}

    virtual bool handleClientSocket() =  0;
    virtual bool handleCommandSocket() =  0;
    virtual bool handleDataSocket() =  0;
    virtual bool handleClientPing() =  0;
    virtual void handleShutdown() = 0;
};

class RawProtocolHandler : public TcpClientProtocolHandler
{
 public:
    RawProtocolHandler(int, int, int, nodename_t, uint32_t);
    virtual ~RawProtocolHandler() {}

    virtual bool handleClientSocket();
    virtual bool handleCommandSocket();
    virtual bool handleDataSocket();
    virtual bool handleClientPing();
    virtual void handleShutdown();
};

class WebSocketProtocolHandler : public TcpClientProtocolHandler
{
    struct Pkt1 {
	uint8_t op;
	uint8_t len;
	uint16_t type;
	uint8_t data[];
    } __attribute__((packed));

    struct Pkt2 {
	uint8_t op;
	uint8_t _126;
	uint16_t len;
	uint16_t type;
	uint8_t data[];
    } __attribute__((packed));

    std::vector<uint8_t> payload;
    bool readPayloadLength(uint8_t, uint64_t&);
    bool readMask(bool, uint32_t&);
    bool handleAcnetCommand(std::vector<uint8_t>&);
    bool sendBinaryDataToClient(Pkt2 *, ssize_t, uint16_t);

 public:
    WebSocketProtocolHandler(int, int, int, nodename_t, uint32_t);
    virtual ~WebSocketProtocolHandler() {}

    virtual bool handleClientSocket();
    virtual bool handleCommandSocket();
    virtual bool handleDataSocket();
    virtual bool handleClientPing();
    virtual void handleShutdown();
};

// Inline functions...

inline uint32_t octetsToIp(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (a << 24) | (b << 16) | (c << 8) | d;
}

inline void secToMs(time_t t, time48_t* t48)
{
    long long tmp = (long long) t * 1000;

    t48->t[0] = htoas((uint16_t) tmp);
    t48->t[1] = htoas((uint16_t) (tmp >> 16));
    t48->t[2] = htoas((uint16_t) (tmp >> 32));
}

// Prototypes...

// Report generation

#ifndef NO_REPORT
void generateReport(TaskPool *);
void generateIpReport(std::ostream&);
void generateReport();
void printElapsedTime(std::ostream&, unsigned);
#endif

// Network interface

int allocSocket(uint32_t, uint16_t, int, int);
int allocClientTcpSocket(uint32_t, uint16_t, int, int);
void dumpIncomingAcnetPackets(bool);
void dumpOutgoingAcnetPackets(bool);
void dumpPacket(const char*, AcnetHeader const&, void const*, size_t);
bool networkInit(uint16_t);
void networkTerm();
DataOut* partialBuffer(trunknode_t);
void generateKillerMessages();
ssize_t readNextPacket(void *, size_t, sockaddr_in&);
int sendDataToNetwork(AcnetHeader const&, void const*, size_t);
void sendErrorToNetwork(AcnetHeader const&, status_t);
void sendKillerMessage(trunknode_t const addr);
void sendNodesRequestUsm(uint32_t);
bool sendPendingPackets();
void sendUsmToNetwork(trunknode_t, taskhandle_t, nodename_t, acnet_taskid_t, uint8_t const*, size_t);
void setPartialBuffer(trunknode_t, DataOut*);
bool validFromAddress(char const[], trunknode_t, uint32_t, uint32_t);
bool validToAddress(char const[], trunknode_t, trunknode_t);

// Node table interface

sockaddr_in const *getAddr(trunknode_t);
bool isMulticastHandle(taskhandle_t);
bool isMulticastNode(trunknode_t);
bool isLocal(trunknode_t, uint32_t = 0);
bool isThisMachine(trunknode_t);
uint32_t ipAddr(char const[]);
time_t lastNodeTableDownloadTime();
uint32_t myIp();
trunknode_t myNode();
nodename_t myHostName();
void setMyHostName(nodename_t);
bool nodeLookup(trunknode_t, nodename_t&);
bool nameLookup(nodename_t, trunknode_t&);
bool nameLookup(nodename_t, uint32_t&);
void updateAddr(trunknode_t, nodename_t, uint32_t);
bool addrLookup(uint32_t, trunknode_t&);
IpInfo* findNodeInfo(trunknode_t);
void setMyIp();
void setLastNodeTableDownloadTime();
bool trunkExists(trunk_t);

// Multicast connections

bool joinMulticastGroup(int, uint32_t);
void dropMulticastGroup(int, uint32_t);
uint32_t countMulticastGroup(uint32_t);

// RAD50

uint32_t ator(char const *);
char const* rtoa(uint32_t, char * = 0);
char const* rtoa_strip(uint32_t, char * = 0);
std::string rtos(nodename_t);

// Misc

bool rejectTask(taskhandle_t const);
uint32_t diffInMs(timeval const&, timeval const&);
void cancelReqToNode(trunknode_t const);
void endRpyToNode(trunknode_t const);

// Global data...

extern bool termSignal;
extern int sNetwork;
extern int sClient;
extern bool dumpIncoming;
extern bool dumpOutgoing;
extern time_t statTimeBase;

// Local Variables:
// mode:c++
// End:
