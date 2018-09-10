#ifndef __SERVER_H
#define __SERVER_H

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
#include "trunknode.h"

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

#define be16(v)	 uint16_t(v)

#else

#define htoas(v) ((uint16_t) (v))
#define htoal(v) ((uint32_t) (v))
#define atohs(v) ((uint16_t) (v))
#define atohl(v) ((uint32_t) (v))

#define be16(v)	 (uint8_t(v >> 8) | uint16_t(v << 8))

#endif

#if THIS_TARGET == Darwin_Target
    #define NO_DAEMON 1
#endif

// These version numbers store the major number in the upper byte and
// the minor in the lower byte. They're in hexadecimal so a version
// number like 0x915 means v9.21.

// Defines the version number of the on-the-wire layout of an ACNET
// packet. There hasn't been an official numbering system (each ACNET
// implementation seemed to choose their own versioning.) We picked
// this number because it was the largest of all the implementations.

#define ACNET_PROTOCOL	0x0915

// Defines the version of the internals of ACNET. This is a
// project-specific version number. For this project, it is the
// version of the source to acnetd. We've been lax at doing this so,
// after 15 years, we're just starting to be more formal about
// releasing new versions.

#define ACNET_INTERNALS	0x0200

// Defines the API that local clients use to communicate with ACNET so
// it's another project-specific version number. For acnetd, this is
// the version number of the command API we use over the local
// loopback socket. In MOOC, it would be the C language API.

#define ACNET_API	0x0900

// Project-wide types and functions

uint32_t ator(char const *);
char const* rtoa(uint32_t, char * = 0);
char const* rtoa_strip(uint32_t, char * = 0);

struct time48_t {
    uint16_t t[3];
} __attribute__((packed));

class reqid_t {
    uint16_t id_;

 public:
    explicit reqid_t() : id_(0) {}
    explicit reqid_t(uint16_t id) : id_(id) {}
    bool operator< (reqid_t const o) const { return id_ < o.id_; }
    bool operator== (reqid_t const o) const { return id_ == o.id_; }
    bool operator!= (reqid_t const o) const { return id_ != o.id_; }
    uint16_t raw() const { return id_; }
};

class rpyid_t {
    uint16_t id_;

 public:
    explicit rpyid_t() : id_(0) {}
    explicit rpyid_t(uint16_t id) : id_(id) {}
    bool operator< (rpyid_t const o) const { return id_ < o.id_; }
    bool operator== (rpyid_t const o) const { return id_ == o.id_; }
    bool operator!= (rpyid_t const o) const { return id_ != o.id_; }
    uint16_t raw() const { return id_; }
};

class taskid_t {
    uint16_t id_;

 public:
    explicit taskid_t(uint16_t id) : id_(id) {}
    bool operator== (taskid_t const o) const { return id_ == o.id_; }
    bool operator!= (taskid_t const o) const { return id_ != o.id_; }
    uint16_t raw() const { return id_; }
};

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
    explicit taskhandle_t(char const*);
    explicit taskhandle_t(std::string const&);

    bool operator< (taskhandle_t const o) const { return h < o.h; }
    bool operator== (taskhandle_t const o) const { return h == o.h; }
    bool operator!= (taskhandle_t const o) const { return h != o.h; }

    bool isBlank() const { return h == 0; }
    uint32_t raw() const { return h; }
    char const* str(char *buf = 0) { return rtoa_strip(h, buf); }
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
    char const* str(char *buf = 0) { return rtoa_strip(h, buf); }
};

class ipaddr_t {
    uint32_t addr;

    friend std::ostream& operator<<(std::ostream& os, ipaddr_t v);

 public:
    ipaddr_t() : addr(0) {}
    explicit ipaddr_t(uint32_t const addr) : addr(addr) {}

    bool isMulticast() const { return IN_MULTICAST(addr); }
    bool isValid() const { return addr != 0; }
    uint32_t value() const { return addr; }
    std::string str() const
    {
	std::ostringstream os;
	os << *this;
	return os.str();
    }

    bool operator< (ipaddr_t const o) const { return addr < o.addr; }
    bool operator== (ipaddr_t const o) const { return addr == o.addr; }
    bool operator!= (ipaddr_t const o) const { return addr != o.addr; }
};

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

struct TaskStats {
    StatCounter usmRcv;
    StatCounter reqRcv;
    StatCounter rpyRcv;
    StatCounter usmXmt;
    StatCounter reqXmt;
    StatCounter rpyXmt;
    StatCounter lostPkt;
};

struct NodeStats {
    StatCounter usmRcv;
    StatCounter reqRcv;
    StatCounter rpyRcv;
    StatCounter usmXmt;
    StatCounter reqXmt;
    StatCounter rpyXmt;
    StatCounter reqQLimit;
};

std::string rtos(nodename_t);


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

#define ACNET_MULTICAST trunknode_t(255)	// Multicast to all nodes

#define	RPY_M_ENDMULT		(0x02)		// terminate multiple reply request
#define	REQ_M_MULTRPY		(0x01)		// multiple reply request

#define N_REQID			2048
#define N_RPYID			2048

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
    AcnetHeader(uint16_t, status_t, trunknode_t, trunknode_t, taskhandle_t, taskid_t, reqid_t, uint16_t);
    uint16_t flags() const { return atohs(flags_); }
    status_t status() const { return status_t(atohs(status_)); }
    trunknode_t client() const { return trunknode_t(trunk_t(size_t(cTrunk_)), node_t(size_t(cNode_))); }
    trunknode_t server() const { return trunknode_t(trunk_t(size_t(sTrunk_)), node_t(size_t(sNode_))); }
    taskhandle_t svrTaskName() const { return taskhandle_t(atohl(svrTaskName_)); }
    taskid_t clntTaskId() const { return taskid_t(atohs(clntTaskId_)); }
    reqid_t msgId() const { return reqid_t(atohs(msgId_)); }
    uint16_t msgLen() const { return atohs(msgLen_); }
    uint8_t const *msg() const { return msg_; }

    void setStatus(status_t status) { status_ = htoas(status.raw()); }
    void setStatus(rpyid_t rpyId) { status_ = htoas(rpyId.raw()); }
    void setFlags(uint16_t flags) { flags_ = htoas(flags); }
    void setClient(trunknode_t tn) { cTrunk_ = tn.trunk().raw(); cNode_ = tn.node(); }
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
extern const status_t ACNET_REQREJ;

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
        cmdKeepAlive	 		= be16(0),

	cmdConnect    			= be16(1),
	cmdConnectExt  			= be16(16),
	cmdRenameTask 			= be16(2),
	cmdDisconnect			= be16(3),

	cmdSend 			= be16(4),
	cmdSendRequest 			= be16(5),
	cmdReceiveRequests		= be16(6),
	cmdSendReply			= be16(7),
	cmdCancel 			= be16(8),
	cmdRequestAck 			= be16(9),

	cmdAddNode 			= be16(10),
	cmdNameLookup 			= be16(11),
	cmdNodeLookup 			= be16(12),
	cmdLocalNode 			= be16(13),

	cmdTaskPid			= be16(14),
	cmdNodeStats			= be16(15),

	cmdDisconnectSingle		= be16(17),

	cmdSendRequestWithTimeout	= be16(18),
	cmdIgnoreRequest        	= be16(19),
	cmdBlockRequests		= be16(20),

	cmdTcpConnect  			= be16(21),

	cmdDefaultNode 			= be16(22)
};

enum class AckList : uint16_t {
	ackAck				= be16(0),

	ackConnect			= be16(1),
	ackConnectExt			= be16(16),

	ackSendRequest			= be16(2),
	ackSendReply			= be16(3),

	ackNameLookup			= be16(4),
	ackNodeLookup			= be16(5),

	ackTaskPid			= be16(6),
	ackNodeStats			= be16(7),
};

// This is the command header for all commands send from the client to
// the acnet task.

#define ASSERT_SIZE(C, S) static_assert(sizeof(C) == S, "Size of "#C" is incorrect")

struct CommandHeader {
 private:
    CommandList const cmd_;
    uint32_t clientName_;
    uint32_t virtualNodeName_;

    CommandHeader();

 public:
    CommandHeader(CommandList Cmd) : cmd_(Cmd), clientName_(0),
				     virtualNodeName_(0) { }

    inline CommandList cmd() const { return cmd_; }
    inline nodename_t virtualNodeName() const { return nodename_t(ntohl(virtualNodeName_)); }
    inline taskhandle_t clientName() const { return taskhandle_t(ntohl(clientName_)); }

    inline void setClientName(taskhandle_t clientName)
    {
	clientName_ = htonl(clientName.raw());
    }

    inline void setVirtualNodeName(nodename_t virtualNodeName)
    {
	virtualNodeName_ = htonl(virtualNodeName.raw());
    }
} __attribute__((packed));

ASSERT_SIZE(CommandHeader, 10);

template<CommandList Cmd>
struct CommandHeaderBase : public CommandHeader {
    CommandHeaderBase() : CommandHeader(Cmd) { }
} __attribute__((packed));

// Sent by a client when it wants to connect to the network. An
// AckConnect is sent back to the client.

struct ConnectCommand : public CommandHeaderBase<CommandList::cmdConnect> {
 private:
    uint32_t pid_;
    uint16_t dataPort_;

 public:
    inline pid_t pid() const { return ntohl(pid_); }
    inline uint16_t dataPort() const { return ntohs(dataPort_); }

    inline void setPid(pid_t pid)
    {
	pid_ = htonl(pid);
    }

    inline void setDataPort(uint16_t port){
	dataPort_ = htons(port);
    }
} __attribute__((packed));

ASSERT_SIZE(ConnectCommand, 16);

struct TcpConnectCommand : public ConnectCommand {
 private:
    uint32_t remoteAddr_;

 public:
    inline ipaddr_t remoteAddr() const { return ipaddr_t(ntohl(remoteAddr_)); }

    inline void setRemoteAddr(ipaddr_t remoteAddr)
    {
	remoteAddr_ = htonl(remoteAddr.value());
    }
} __attribute__((packed));

ASSERT_SIZE(TcpConnectCommand, 20);

// Sent by a client periodicly to keep it's Acnet connection.  An
// AckCommand is sent back to the client.

struct KeepAliveCommand : public CommandHeaderBase<CommandList::cmdKeepAlive> {
} __attribute__((packed));

ASSERT_SIZE(KeepAliveCommand, 10);

// Sent by a client when it wants to rename a connected task. An
// AckConnect is sent back to the client.

struct RenameTaskCommand : public CommandHeaderBase<CommandList::cmdRenameTask> {
 private:
    uint32_t newName_;

 public:
    inline taskhandle_t newName() const { return taskhandle_t(ntohl(newName_)); }
} __attribute__((packed));

ASSERT_SIZE(RenameTaskCommand, 14);

// Sent by a client when it wants to disconnect from the network. An
// AckCommand is sent back to the client.

struct DisconnectCommand :
    public CommandHeaderBase<CommandList::cmdDisconnect> {
} __attribute__((packed));

ASSERT_SIZE(DisconnectCommand, 10);

struct DisconnectSingleCommand :
    public CommandHeaderBase<CommandList::cmdDisconnectSingle> {
} __attribute__((packed));

ASSERT_SIZE(DisconnectSingleCommand, 10);

// Sent by a client wanting to send an USM. An AckCommand is sent to
// the client.

struct SendCommand : public CommandHeaderBase<CommandList::cmdSend> {
 private:
    uint32_t task_;
    uint16_t addr_;
    uint8_t data_[];

 public:
    inline trunknode_t addr() const { return trunknode_t(ntohs(addr_)); }
    inline taskhandle_t task() const { return taskhandle_t(ntohl(task_)); }
    inline uint8_t const *data() const { return data_; }
} __attribute__((packed));

ASSERT_SIZE(SendCommand, 16);

// Sent by a client that wants to be a "RUM listener". The acnet task
// marks the task handle (previously registered with a ConnectCommand)
// as able to receive.

struct ReceiveRequestCommand :
    public CommandHeaderBase<CommandList::cmdReceiveRequests> {
} __attribute__((packed));

ASSERT_SIZE(ReceiveRequestCommand, 10);

// Sent by a client that wants to do a lookup of a node name to its
// trunk/node combo.

struct NameLookupCommand :
    public CommandHeaderBase<CommandList::cmdNameLookup> {

 private:
    uint32_t name_;

 public:
    inline nodename_t name() const { return nodename_t(ntohl(name_)); }
} __attribute__((packed));

ASSERT_SIZE(NameLookupCommand, 14);

// Sent by a client that wants to do a lookup of a node name from its
// trunk/node combo.

struct NodeLookupCommand :
    public CommandHeaderBase<CommandList::cmdNodeLookup> {

 private:
    uint16_t addr_;

 public:
    inline trunknode_t addr() const { return trunknode_t(ntohs(addr_)); }
} __attribute__((packed));

ASSERT_SIZE(NodeLookupCommand, 12);

struct LocalNodeCommand :
    public CommandHeaderBase<CommandList::cmdLocalNode> {
} __attribute__((packed));

ASSERT_SIZE(LocalNodeCommand, 10);

struct DefaultNodeCommand :
    public CommandHeaderBase<CommandList::cmdDefaultNode> {
} __attribute__((packed));

ASSERT_SIZE(DefaultNodeCommand, 10);

struct SendRequestCommand :
    public CommandHeaderBase<CommandList::cmdSendRequest> {

 private:
    uint32_t task_;
    uint16_t addr_;
    uint16_t flags_;
    uint8_t data_[];

 public:
    inline trunknode_t addr() const { return trunknode_t(ntohs(addr_)); }
    inline taskhandle_t task() const { return taskhandle_t(ntohl(task_)); }
    inline uint16_t flags() const { return ntohs(flags_); }
    inline uint8_t const *data() const { return data_; }
} __attribute__((packed));

ASSERT_SIZE(SendRequestCommand, 18);

struct SendRequestWithTimeoutCommand :
    public CommandHeaderBase<CommandList::cmdSendRequestWithTimeout> {

 private:
    uint32_t task_;
    uint16_t addr_;
    uint16_t flags_;
    uint32_t timeout_;
    uint8_t data_[];

 public:
    inline trunknode_t addr() const { return trunknode_t(ntohs(addr_)); }
    inline taskhandle_t task() const { return taskhandle_t(ntohl(task_)); }
    inline uint16_t flags() const { return ntohs(flags_); }
    inline uint32_t timeout() const { return ntohl(timeout_); }
    inline uint8_t const *data() const { return data_; }
} __attribute__((packed));

ASSERT_SIZE(SendRequestWithTimeoutCommand, 22);

struct SendReplyCommand : public CommandHeaderBase<CommandList::cmdSendReply> {
 private:
    uint16_t rpyid_;
    uint16_t flags_;
    int16_t status_;
    uint8_t data_[];

 public:
    inline rpyid_t rpyid() const { return rpyid_t(ntohs(rpyid_)); }
    inline uint16_t flags() const { return ntohs(flags_); }
    inline status_t status() const { return status_t(ntohs(status_)); }
    inline uint8_t const *data() const { return data_; }
} __attribute__((packed));

ASSERT_SIZE(SendReplyCommand, 16);

struct IgnoreRequestCommand :
    public CommandHeaderBase<CommandList::cmdIgnoreRequest> {

 private:
    uint16_t rpyid_;

 public:
    inline rpyid_t rpyid() const { return rpyid_t(ntohs(rpyid_)); }
} __attribute__((packed));

ASSERT_SIZE(IgnoreRequestCommand, 12);

struct CancelCommand : public CommandHeaderBase<CommandList::cmdCancel> {
 private:
    uint16_t reqid_;

 public:
    inline reqid_t reqid() const { return reqid_t(ntohs(reqid_)); }
} __attribute__((packed));

ASSERT_SIZE(CancelCommand, 12);

struct BlockRequestCommand :
    public CommandHeaderBase<CommandList::cmdBlockRequests> {
} __attribute__((packed));

ASSERT_SIZE(BlockRequestCommand, 10);

struct RequestAckCommand :
    public CommandHeaderBase<CommandList::cmdRequestAck> {

 private:
    uint16_t rpyid_;

 public:
    inline rpyid_t rpyid() const { return rpyid_t(ntohs(rpyid_)); }
} __attribute__((packed));

ASSERT_SIZE(RequestAckCommand, 12);

struct AddNodeCommand : public CommandHeaderBase<CommandList::cmdAddNode> {
 private:
    uint32_t ipAddr_;
    uint32_t flags_;
    uint16_t addr_;
    uint32_t nodeName_;

 public:
    inline ipaddr_t ipAddr() const { return ipaddr_t(ntohl(ipAddr_)); }
    inline uint32_t flags() const { return ntohl(flags_); }
    inline trunknode_t addr() const { return trunknode_t(ntohs(addr_)); }
    inline nodename_t nodeName() const { return nodename_t(ntohl(nodeName_)); }
} __attribute__((packed));

ASSERT_SIZE(AddNodeCommand, 24);

struct TaskPidCommand : public CommandHeaderBase<CommandList::cmdTaskPid> {
    uint32_t task;
} __attribute__((packed));

ASSERT_SIZE(TaskPidCommand, 14);

struct NodeStatsCommand :
    public CommandHeaderBase<CommandList::cmdNodeStats> {
} __attribute__((packed));

ASSERT_SIZE(NodeStatsCommand, 10);

class AckHeader {
 private:
    AckList const cmd_;
    int16_t status_;

    AckHeader();

 public:
    AckHeader(AckList cmd) : cmd_(cmd) { setStatus(ACNET_SUCCESS); }

    AckList cmd() const { return cmd_; }
    status_t status() const { return status_t(ntohs(status_)); }
    void setStatus(status_t status) { status_ = htons(status.raw()); }
} __attribute__((packed));

ASSERT_SIZE(AckHeader, 4);

// This command is only sent from the acnet task to the clients to
// acknowledge a command. This class contains a status field to pass
// the success or failure codes back to the client.

struct Ack : public AckHeader {
    Ack() : AckHeader(AckList::ackAck) { }
} __attribute__((packed));

ASSERT_SIZE(Ack, 4);

struct AckConnect : public AckHeader {
 private:
    uint8_t id_;
    uint32_t clientName_;

 public:
    AckConnect() : AckHeader(AckList::ackConnect) { }
    void setTaskId(taskid_t id) { id_ = (uint8_t) id.raw(); }
    void setClientName(taskhandle_t clientName) { clientName_ = htonl(clientName.raw()); }
} __attribute__((packed));

ASSERT_SIZE(AckConnect, 9);

struct AckConnectExt : public AckHeader {
 private:
    uint16_t id_;
    uint32_t clientName_;

 public:
    AckConnectExt() : AckHeader(AckList::ackConnectExt) { }
    void setTaskId(taskid_t id) { id_ = htons(id.raw()); }
    void setClientName(taskhandle_t clientName) { clientName_ = htonl(clientName.raw()); }
} __attribute__((packed));

ASSERT_SIZE(AckConnectExt, 10);

struct AckSendRequest : public AckHeader {
 private:
    uint16_t reqid_;

 public:
    AckSendRequest() : AckHeader(AckList::ackSendRequest) { }
    void setRequestId(reqid_t reqid) { reqid_ = htons(reqid.raw()); }
} __attribute__((packed));

ASSERT_SIZE(AckSendRequest, 6);

struct AckSendReply : public AckHeader {
 private:
    uint16_t flags;

 public:
    AckSendReply() : AckHeader(AckList::ackSendReply) { }
} __attribute__((packed));

ASSERT_SIZE(AckSendReply, 6);

struct AckNameLookup : public AckHeader {
 private:
    uint8_t trunk;
    uint8_t node;

 public:
    AckNameLookup() : AckHeader(AckList::ackNameLookup) { }
    void setTrunkNode(trunknode_t addr) { trunk = addr.trunk().raw(); node = addr.node(); }
} __attribute__((packed));

ASSERT_SIZE(AckNameLookup, 6);

struct AckNodeLookup : public AckHeader {
 private:
    uint32_t name_;

 public:
    AckNodeLookup() : AckHeader(AckList::ackNodeLookup) { }
    void setNodeName(nodename_t name) { name_ = htonl(name.raw()); }
} __attribute__((packed));

ASSERT_SIZE(AckNodeLookup, 8);

struct AckTaskPid : public AckHeader {
 private:
    uint32_t pid_;

 public:
    AckTaskPid() : AckHeader(AckList::ackTaskPid) { }
    void setPid(pid_t pid) { pid_ = htonl(pid); }
} __attribute__((packed));

ASSERT_SIZE(AckTaskPid, 8);

struct AckNodeStats : public AckHeader {
 private:
    uint32_t statUsmRcv;
    uint32_t statReqRcv;
    uint32_t statRpyRcv;
    uint32_t statUsmXmt;
    uint32_t statReqXmt;
    uint32_t statRpyXmt;
    uint32_t statReqQLimit;

 public:
    AckNodeStats() : AckHeader(AckList::ackNodeStats) { }
    void setStats(NodeStats& stats)
    {
	statUsmRcv = htonl((uint32_t) stats.usmRcv);
	statReqRcv = htonl((uint32_t) stats.reqRcv);
	statRpyRcv = htonl((uint32_t) stats.rpyRcv);
	statUsmXmt = htonl((uint32_t) stats.usmXmt);
	statReqXmt = htonl((uint32_t) stats.reqXmt);
	statRpyXmt = htonl((uint32_t) stats.rpyXmt);
	statReqQLimit = htonl((uint32_t) stats.reqQLimit);
    }
} __attribute__((packed));

ASSERT_SIZE(AckNodeStats, 32);

// Asynchronous message passing to client processes

struct AcnetClientMessage {
 private:
    uint32_t pid_;
    uint32_t task_;
    uint8_t type;

 public:
    enum {
	Ping,
	DumpProcessIncomingPacketsOn,
	DumpProcessIncomingPacketsOff,
	DumpTaskIncomingPacketsOn,
	DumpTaskIncomingPacketsOff,
    };

    AcnetClientMessage() : pid_(0), task_(0), type(Ping) {}
    AcnetClientMessage(taskhandle_t task, uint8_t type) : task_(htoal(task.raw())), type(type) {}
    void setPid(pid_t pid) { pid_ = htoal(pid); }
    taskhandle_t task() { return taskhandle_t(atohl(task_)); }
} __attribute__((packed));

ASSERT_SIZE(AcnetClientMessage, 9);

// Project-wide types...

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
    uint16_t ids[N_REQID];
};

struct AcnetRpyList {
    uint16_t total;
    uint16_t ids[N_RPYID];
};

#include "timesensitive.h"

class RequestPool;

extern Node RequestRoot;

// This class encompasses all the information related to request ids.

class ReqInfo : public TimeSensitive<RequestRoot> {
 friend class IdPool<ReqInfo, reqid_t, N_REQID>;
 friend class RequestPool;

 private:
    ReqInfo() : task_(0), flags(0), tmoMs(0), initTime_(0) {}

    TaskInfo* task_;
    taskhandle_t taskName_;
    trunknode_t lclNode_;
    trunknode_t remNode_;
    uint16_t flags;
    DeltaTime tmoMs;
    bool mcast;
    AbsTime initTime_;

public:
    mutable StatCounter totalPackets;

    reqid_t id() const;
    TaskInfo& task() const		{ return *task_; }
    void bumpPktStats() const		{ ++totalPackets; }
    bool wantsMultReplies() const	{ return flags & ACNET_FLG_MLT; }
    bool multicasted() const		{ return mcast; }
    AbsTime expiration() const		{ return lastUpdate + tmoMs; }
    taskhandle_t taskName() const	{ return taskName_; }
    trunknode_t lclNode() const		{ return lclNode_; }
    trunknode_t remNode() const		{ return remNode_; }
    AbsTime initTime() const		{ return initTime_; }
};

class RequestPool {
 friend class ReqInfo;

 private:
    IdPool<ReqInfo, reqid_t, N_REQID> idPool;
    void release(ReqInfo *);

 public:
    ReqInfo *alloc(TaskInfo*, taskhandle_t, trunknode_t, trunknode_t, uint16_t,
		   DeltaTime);

    bool cancelReqId(reqid_t, bool = true, bool = false);
    void cancelReqToNode(trunknode_t const);
    DeltaTime sendRequestTimeoutsAndGetNextTimeout();

    ReqInfo *next(ReqInfo const * const req) const 	{ return idPool.next(req); }
    ReqInfo *entry(reqid_t const id) 			{ return idPool.entry(id); }

    ReqInfo *oldest();
    void update(ReqInfo* req) 				{ req->update(); }

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

extern Node ReplyRoot;

// This class encompasses all the information related to reply ids.

class RpyInfo : public TimeSensitive<ReplyRoot> {
 friend class IdPool<RpyInfo, rpyid_t, N_RPYID>;
 friend class ReplyPool;

 private:
    RpyInfo() : task_(0), flags(0), taskId_(0), initTime_(0) {}

    TaskInfo* task_;
    uint16_t flags;
    trunknode_t lclNode_;
    trunknode_t remNode_;
    taskhandle_t taskName_;
    taskid_t taskId_;
    bool mcast;
    reqid_t reqId_;
    AbsTime initTime_;
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
    AbsTime expiration() const 		{ return lastUpdate + DeltaTime(REPLY_DELAY * 1000); }
    taskhandle_t taskName() const	{ return taskName_; }
    taskid_t taskId() const		{ return taskId_; }
    trunknode_t lclNode() const		{ return lclNode_; }
    trunknode_t remNode() const		{ return remNode_; }
    reqid_t reqId() const		{ return reqId_; }
    AbsTime initTime() const		{ return initTime_; }

    bool xmitReply(status_t, void const*, size_t, bool);
};

class ReplyPool {
 friend class RpyInfo;

    #ifdef PINGER
    typedef std::map<trunknode_t, unsigned> ActiveTargetMap;

    class Pinger {
	AbsTime expires;
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
    class request_key_t {
	uint32_t key_;

     public:
	explicit request_key_t(trunknode_t n, reqid_t id) :
			key_((uint32_t(n.raw()) << 16) | uint32_t(id.raw())) {}
	bool operator< (request_key_t const o) const { return key_ < o.key_; }
	bool operator== (request_key_t const o) const { return key_ == o.key_; }
	bool operator!= (request_key_t const o) const { return key_ != o.key_; }
    };

    typedef std::multimap<request_key_t, RpyInfo*> ActiveMap;
    typedef std::pair<ActiveMap::iterator, ActiveMap::iterator> ActiveRangeIterator;

    IdPool<RpyInfo, rpyid_t, N_REQID> idPool;

    ActiveMap activeMap;

    #ifdef PINGER
    ActiveTargetMap targetMap;
    Pinger pinger;
    #endif

    void release(RpyInfo *);

 public:
    #ifdef PINGER
    ReplyPool() : pinger(targetMap) {};
    #else
    ReplyPool() {};
    #endif

    RpyInfo *alloc(TaskInfo *, reqid_t, taskid_t, taskhandle_t, trunknode_t, trunknode_t, uint16_t);

    RpyInfo* rpyInfo(rpyid_t);
    RpyInfo* rpyInfo(trunknode_t, reqid_t);

    RpyInfo *next(RpyInfo const * const rpy) const 	{ return idPool.next(rpy); }

    status_t sendReplyToNetwork(TaskInfo const*, rpyid_t, status_t, void const*, size_t, bool);
    void endRpyToNode(trunknode_t const);
    void endRpyId(rpyid_t, status_t = ACNET_SUCCESS);
    DeltaTime sendReplyPendsAndGetNextTimeout();

    RpyInfo *getOldest();
    void update(RpyInfo* rpy) 		{ rpy->update(); }

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

#define	MAX_TASKS		(1024)

// TaskInfo
//
// Information related to all connected  tasks.

class TaskInfo : private Noncopyable {
    const AbsTime boot;
    TaskPool& taskPool_;
    taskhandle_t handle_;
    const taskid_t id_;

    static unsigned const maxPendingRequestsAccepted = 256;

    unsigned pendingRequests;
    unsigned maxPendingRequests;

    TaskInfo();

 protected:
    ReqList requests;
    RpyList replies;

 public:
    friend class TaskPool;

    mutable TaskStats stats;

    TaskInfo(TaskPool&, taskhandle_t, taskid_t);
    virtual ~TaskInfo() {}

    void setHandle(taskhandle_t th) { handle_ = th; }

    // Informational

    DeltaTime connectedTime() const;
    virtual pid_t pid() const = 0;
    virtual bool acceptsUsm() const = 0;
    virtual bool acceptsRequests() const = 0;
    virtual bool isPromiscuous() const  = 0;
    virtual bool needsToBeThrottled() const = 0;
    virtual bool stillAlive(DeltaTime = DeltaTime(0)) const = 0;
    virtual bool equals(TaskInfo const* o) const = 0;
    TaskPool& taskPool() const	{ return taskPool_; }
    taskhandle_t handle() const	{ return handle_; }
    taskid_t id() const 	{ return id_; }
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

// InternalTask
//
// Basis for all tasks connection from within acnetd
//
class InternalTask : public TaskInfo {
    void sendReplyCore(rpyid_t, void const*, uint16_t, status_t, uint16_t);

 protected:
    void _clear_receiving() { }

 public:
    InternalTask(TaskPool& taskPool, taskhandle_t handle, taskid_t id) :
	TaskInfo(taskPool, handle, id) { }
    virtual ~InternalTask() {}

    pid_t pid() const;
    bool acceptsUsm() const { return true; }
    bool acceptsRequests() const { return true; }
    bool needsToBeThrottled() const { return false; }
    void commandReceived() const { }
    bool stillAlive(DeltaTime) const { return true; }

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
    void activeReplies(rpyid_t, uint8_t, uint16_t const* const, size_t);
    void packetCountHandler(rpyid_t);
    void taskIdHandler(rpyid_t, uint16_t const* const, uint16_t);
    void taskNameHandler(rpyid_t, uint8_t);
    void taskNameHandler(rpyid_t, uint16_t const* const, uint16_t);
    void killerMessageHandler(rpyid_t, uint8_t, uint16_t const* const, uint16_t);
    void tasksHandler(rpyid_t, uint8_t);
    void pingHandler(rpyid_t);
    void taskResourcesHandler(rpyid_t);
    void resetStats();
    void nodeStatsHandler(rpyid_t, uint8_t);
    void tasksStatsHandler(rpyid_t, uint8_t);
    void ipNodeTableHandler(rpyid_t, uint8_t, const uint16_t*, size_t);
    void timeHandler(rpyid_t, uint8_t);
    void debugHandler(rpyid_t, uint8_t, const uint16_t*, uint16_t);
    void activeRequests(rpyid_t, uint8_t, const uint16_t*, size_t);
    void replyDetail(rpyid_t, const uint16_t*, size_t);
    void requestDetail(rpyid_t, const uint16_t*, size_t);
    void requestReport(rpyid_t);

 public:
    AcnetTask(TaskPool&, taskid_t);
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
    AbsTime taskStatTimeBase;
    trunknode_t node_;
    nodename_t nodeName_;
    TaskInfo *tasks_[MAX_TASKS];
    TaskHandleMap active;
    TaskList removed;

    taskid_t nextFreeTaskId(ConnectCommand const* const);
    void removeInactiveTasks();

 public:
    RequestPool reqPool;
    ReplyPool rpyPool;

    mutable NodeStats stats;

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
    TaskInfo* getTask(taskid_t) const;
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

#define TCP_CLIENT_PING	(0)
#define ACNETD_COMMAND	(1)
#define ACNETD_ACK	(2)
#define ACNETD_DATA	(3)

class TcpClientProtocolHandler : private Noncopyable
{
    int getSocketPort(int);

 public:
    enum Traffic { AckTraffic, AllTraffic };

 protected:
    int sTcp, sCmd, sData;
    nodename_t tcpNode;
    ipaddr_t remoteAddr;

    Traffic enabledTraffic;

    bool readBytes(void *, size_t);
    bool handleClientCommand(CommandHeader *, size_t);

    virtual bool handleCommandSocket() =  0;

 public:
    TcpClientProtocolHandler(int, int, int, nodename_t, ipaddr_t);
    virtual ~TcpClientProtocolHandler() {}

    Traffic whichTraffic() const { return enabledTraffic; }

    virtual bool handleClientSocket() =  0;
    virtual bool handleDataSocket() =  0;
    virtual bool handleClientPing() =  0;
    virtual void handleShutdown() = 0;

    bool commandSocketData();
};

class RawProtocolHandler : public TcpClientProtocolHandler
{
    virtual bool handleCommandSocket();

 public:
    RawProtocolHandler(int, int, int, nodename_t, ipaddr_t);
    virtual ~RawProtocolHandler() {}

    virtual bool handleClientSocket();
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

    virtual bool handleCommandSocket();

 public:
    WebSocketProtocolHandler(int, int, int, nodename_t, ipaddr_t);
    virtual ~WebSocketProtocolHandler() {}

    virtual bool handleClientSocket();
    virtual bool handleDataSocket();
    virtual bool handleClientPing();
    virtual void handleShutdown();
};

// Inline functions...

inline ipaddr_t octetsToIp(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return ipaddr_t((a << 24) | (b << 16) | (c << 8) | d);
}

inline void toTime48(DeltaTime const t, time48_t* t48)
{
    int64_t ms = t.get_msec();

    t48->t[0] = htoas(uint16_t(ms));
    t48->t[1] = htoas(uint16_t(ms >> 16));
    t48->t[2] = htoas(uint16_t(ms >> 32));
}

// Prototypes...

// Report generation

#ifndef NO_REPORT
void generateReport(TaskPool *);
void generateIpReport(std::ostream&);
void generateReport();
void printElapsedTime(std::ostream&, int64_t);
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
void sendUsmToNetwork(trunknode_t, taskhandle_t, nodename_t, taskid_t, uint8_t const*, size_t);
void setPartialBuffer(trunknode_t, DataOut*);
bool validFromAddress(char const[], trunknode_t, ipaddr_t, ipaddr_t);
bool validToAddress(char const[], trunknode_t, trunknode_t);

// Node table interface

sockaddr_in const *getAddr(trunknode_t);
bool isMulticastHandle(taskhandle_t);
bool isMulticastNode(trunknode_t);
bool isLocal(trunknode_t, uint32_t = 0);
bool isThisMachine(trunknode_t);
uint32_t ipAddr(char const[]);
AbsTime lastNodeTableDownloadTime();
ipaddr_t myIp();
trunknode_t myNode();
nodename_t myHostName();
void setMyHostName(nodename_t);
bool nodeLookup(trunknode_t, nodename_t&);
bool nameLookup(nodename_t, trunknode_t&);
bool nameLookup(nodename_t, ipaddr_t&);
void updateAddr(trunknode_t, nodename_t, ipaddr_t);
bool addrLookup(ipaddr_t, trunknode_t&);
void setMyIp();
void setLastNodeTableDownloadTime();
bool trunkExists(trunk_t);

// Multicast connections

bool joinMulticastGroup(int, ipaddr_t);
void dropMulticastGroup(int, ipaddr_t);
uint32_t countMulticastGroup(ipaddr_t);

// Misc

bool rejectTask(taskhandle_t const);
void cancelReqToNode(trunknode_t const);
void endRpyToNode(trunknode_t const);

// Global data...

extern const taskid_t AcnetTaskId;
extern bool termSignal;
extern int sNetwork;
extern int sClient;
extern bool dumpIncoming;
extern bool dumpOutgoing;
extern AbsTime statTimeBase;

// Local Variables:
// mode:c++
// End:

#endif
