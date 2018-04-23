#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include "server.h"

// This is our base exception class. It's an improvement over the
// standard exceptions because you can pass in a printf() like message
// to the constructor.

class regression : public std::exception {
    char msg[256];

 public:
    regression(char const* fmt, ...) throw()
    {
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);
    }

    static char const* context;

    char const* what() const throw() { return msg; }
};

class AcnetPacket {
    std::vector<uint8_t> packet;

 public:
    template <class Iterator>
    void assign(Iterator b, Iterator e)
	{
	    packet.clear();
	    packet.assign(b, e);
	    if (packet.size() < AcnetHeaderSize)
		throw regression("received packet smaller than AcnetHeader (size = %u)", packet.size());
	}

    operator AcnetHeader () const { return AcnetHeader(&packet[0]); }
    std::vector<uint8_t>::const_iterator begin() const { return packet.begin() + AcnetHeaderSize); }
    std::vector<uint8_t>::const_iterator end() const { return packet.end(); }
};

// This is a socket managing class. It makes sure we don't leak socket
// handles.

class SockMan {
    int const s;

 public:
    explicit SockMan(uint32_t addr = INADDR_LOOPBACK, uint16_t port = INADDR_ANY) :
	s(socket(AF_INET, SOCK_DGRAM, 0))
	{
	    if (-1 != s) {
		sockaddr_in in;

		// Bind the socket to the requested port and address.

#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
		in.sin_len = sizeof(in);
#endif
		in.sin_family = AF_INET;
		in.sin_port = htons(port);
		in.sin_addr.s_addr = htonl(addr);

		if (-1 != bind(s, reinterpret_cast<sockaddr*>(&in), sizeof(in))) {
#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
		    in.sin_len = sizeof(in);
#endif
		    in.sin_family = AF_INET;
		    in.sin_port = htons(ACNET_CLIENT_PORT);
		    in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		    if (-1 == connect(s, reinterpret_cast<sockaddr*>(&in), sizeof(in))) {
			close(s);
			throw regression("couldn't connect to server -- error %d", errno);
		    }
		} else {
		    close(s);
		    throw regression("couldn't bind to port %d -- error %d", port, errno);
		}
	    } else
		throw regression("couldn't create a new socket -- error %d", errno);
	}

    SockMan(SockMan const& o) :
	s(dup(o.s))
	{
	    if (-1 == s)
		throw regression("couldn't duplicate socket handle -- error %d", errno);
	}

    ~SockMan() { close(s); }
    operator int() const { return s; }
};

char const* regression::context = "<not set>";

// Local prototypes...

template <short>
static void disconnect(int, taskhandle_t) throw();
template <class C, class R>
static void performCommand(int, C const&, R&) throw(regression);
template <class T>
static void sendPacket(int s, T const& buf) throw(regression);
static reqid_t sendRequest(int, char const*, uint16_t, bool, uint32_t, void const*, size_t);
static void testCleanup() throw(regression);
static void testCommandInterface() throw(regression);
static void testConnects() throw(regression);
static void waitForNoResponse(int, int = 250) throw(regression);
template <class T>
static size_t waitForResult(int, T&, int = 250) throw(regression);
static size_t waitForResult(int, void*, size_t, int) throw(regression);

// Local data...

static char const thValidator[] = "VLDTOR";
static SockMan sCmd1, sData1, sCmd2, sData2;

// This function makes an ACNET connection.

template <short res>
static void makeConnection(char const* handle, int sCmd, int sData)
{
    sockaddr_in in;
    socklen_t len = sizeof(in);

    if (-1 == getsockname(sData, (sockaddr*) &in, &len))
	throw regression("error making '%s' connection -- getsockname() returned %d",
			 handle, errno);

    ConnectCommand cmd;

    cmd.clientName = htonl(ator(handle));
    cmd.pid = htonl(getpid());
    cmd.dataPort = in.sin_port;

    AckConnect ack;

    performCommand(sCmd, cmd, ack);
    short const status = (short) ntohl(ack.status);
    if (status != res)
	throw regression("error making '%s' connection -- expected [%d %d], received [%d %d]",
			 handle, res & 0xff, res >> 8, status & 0xff, status >> 8);
}

template <short res>
static bool getPacket(int s, AcnetPacket& pkt, int tmo)
{
    pollfd pfd;

    pfd.fd = s;
    pfd.events = POLLRDNORM;
    pfd.revents = 0;

  loop:
    switch (poll(&pfd, 1, tmo)) {
     case 1:
     {
	 char buffer[8192];
	 ssize_t const len = recv(s, buffer, sizeof(buffer), 0);

	 if (-1 == len)
	     throw regression("recv() returned error -- %d", errno);
	 pkt.assign(buffer, buffer + len);

	 short const status = ((AcnetHeader) pkt).status();

	 if (status != res)
	     throw regression("unexpected status in incoming ACNET packet -- "
			      "expected [%d %d], received [%d %d]",
			      res & 0xff, res >> 8, status & 0xff, status >> 8);
	 return true;
     }
     break;

     case 0:
	return false;;

     case -1:
	if (errno == EINTR)
	    goto loop;
	throw regression("poll() returned error -- %d", errno);

     default:
	throw regression("poll() returned illegal value");
    }
}

template <short res>
static void disconnect(int s, taskhandle_t th) throw()
{
    DisconnectCommand cmd;
    Ack ack;

    cmd.clientName = htonl(th);
    performCommand(s, cmd, ack);
    short const status = (short) ntohl(ack.status);
    if (status != res)
	throw regression("error disconnecting -- expected [%d %d], received [%d %d]",
			 res & 0xff, res >> 8, status & 0xff, status >> 8);
}

template <class C, class R>
static void performCommand(int s, C const& cmd, R& res) throw(regression)
{
    sendPacket(s, cmd);

    size_t const len = waitForResult(s, res);

    if (len >= sizeof(Ack)) {
	if (cmd.seq != res.seq)
	    throw regression("sequence numbers don't match -> %d != %d", ntohs(cmd.seq), ntohs(res.seq));
	if (ntohl(res.status) == ACNET_SUCCESS && len != sizeof(R))
	    throw regression("got ACNET_SUCCESS response, but incorrectly sized reply -- %d bytes instead of %d", len, sizeof(R));
    } else
	throw regression("reply was smaller than an Ack (%d bytes)", len);
}

template <class T>
static reqid_t sendRequest(int sCmd, char const* handle, uint16_t target, bool mult, uint32_t tmo, T const& buf)
{
    return sendRequest(sCmd, handle, target, mult, tmo, reinterpret_cast<void const*>(&buf), sizeof(T));
}

static reqid_t sendRequest(int sCmd, char const* handle, uint16_t target, bool mult, uint32_t tmo, void const* data, size_t n)
{
    char* const ptr = new char[sizeof(SendRequestWithTmoCommand) + n];
    SendRequestWithTmoCommand& cmd = *reinterpret_cast<SendRequestWithTmoCommand*>(ptr);

    cmd.clientName = htonl(ator(thValidator));
    cmd.task = htonl(ator(handle));
    cmd.node = htons(target);
    cmd.flags = mult ? htons(1) : htons(0);
    cmd.tmo = htonl(tmo);
    std::copy(reinterpret_cast<uint8_t const*>(data),
	      reinterpret_cast<uint8_t const*>(data) + n,
	      cmd.data);

    try {
	AckSendRequest ack;

	performCommand(sCmd, cmd, ack);

	short const status = (short) ntohl(ack.status);

	if (status != ACNET_SUCCESS)
	    throw regression("error sending request -- received [%d %d]",
			     status & 0xff, status >> 8);
	delete [] ptr;
	return htons(ack.reqid);
    }
    catch (...) {
	delete [] ptr;
	throw;
    }
}

// This frees up the sockets.

static void releaseSockets() throw()
{
    if (sCmd1 != -1)
	close(sCmd1);

    if (sData1 != -1)
	close(sData1);

    if (sCmd2 != -1)
	close(sCmd2);

    if (sData2 != -1)
	close(sData2);
}

template <class T>
static void sendPacket(int s, T const& buf) throw(regression)
{
    int const res = send(s, &buf, sizeof(T), 0);

    if (-1 == res)
	throw regression("error writing to socket -- %d", errno);
    if (sizeof(T) != res)
	throw regression("only wrote %d of %d bytes", res, sizeof(T));
}

static void testCommandInterface() throw(regression)
{
    // Send a packet that is too small to be valid.

    CommandHeader buf(0);

    regression::context = "testing valid command length (TP-2)";
    for (size_t ii = 0; ii < sizeof(buf); ++ii) {
	int const res = send(sCmd1, &buf, ii, 0);

	if (-1 == res)
	    throw regression("error writing to socket -- %d", errno);
	if (ii != (size_t) res)
	    throw regression("only wrote %d of %d bytes", res, ii);

	waitForNoResponse(sCmd1);
    }
    std::cerr << "++++ command sanity checks are working\n";
}

static void testCleanup() throw(regression)
{
    regression::context = "testing automatic clean-up";

#if 0
    // For this first test, we create another client (using the third
    // set of sockets.) This client represents a client that connected
    // and then went away. We need to make sure acnetd cleans up
    // properly when a client goes away. We've already tested the case
    // when we're trying to connect using a handle that another client
    // used. This test tries to clean up all resources when it is
    // determined a client has died.

    sCmd3 = allocSocket();
    sData3 = allocSocket();

    makeConnection<ACNET_SUCCESS>("TEST01", sCmd3, sData3);
    makeConnection<ACNET_SUCCESS>("TEST02", sCmd3, sData3);

    close(sCmd3);
    close(sData3);

    // Right now, acnetd's tables should still indicate that TEST01
    // and TEST02 are connection points. Now we're going to talk to
    // ACNET to get a task list. ACNET will ping each task as it
    // builds it's reply. When it hits TEST01 or TEST02, it should
    // clean up everything.
#endif
}

static void testConnects() throw(regression)
{
    // Send an incorrectly sized structure for the connect command.

    regression::context = "testing Connect command (TP-3)";

    CommandHeader const hdr(cmdConnect);
    AckConnect ack;

    performCommand(sCmd1, hdr, ack);
    if (ntohl(ack.status) != (uint32_t) ACNET_INVARG)
	throw regression("bad length should return ACNET_INVARG, returned 0x%08x", ntohl(ack.status));

    // Send in a bad data port to the connect command (we need a valid
    // port to receive ACNET packets.)

    regression::context = "testing Connect command (TP-4)";

    ConnectCommand cmd;

    cmd.clientName = htonl(ator(thValidator));
    cmd.pid = htonl(getpid());
    cmd.dataPort = htons(0);

    performCommand(sCmd1, cmd, ack);
    if (ntohl(ack.status) != (uint32_t) ACNET_INVARG)
	throw regression("null data socket should return ACNET_INVARG, returned 0x%08x", ntohl(ack.status));

    // Try to connect as VLDTOR

    regression::context = "testing Connect command (TP-5)";

    {
	SockMan const sCmd;
	SockMan const sData;

	makeConnection<ACNET_SUCCESS>(thValidator, sCmd, sData);

	// Try to connect as VLDTOR using the other client
	// socket. acnetd should ping our first client socket to see
	// if it's still alive.  Since we don't have to respond to the
	// PING, we can clean it up after we've verified the return
	// status.

	regression::context = "testing Connect command (TP-6)";

	cmd.dataPort = htons(sData1);
	performCommand(sCmd1, cmd, ack);
	if (ntohl(ack.status) != (uint32_t) ACNET_NAME_IN_USE)
	    throw regression("connecting as other client should return ACNET_NAME_IN_USE, returned 0x%08x", ntohl(ack.status));

	regression::context = "testing Connect command (TP-8)";

	for (int ii = 0; ii < MAX_TASKS - 2; ++ii) {
	    char handle[7];

	    snprintf(handle, sizeof(handle), "TMP%03d", ii);
	    makeConnection<ACNET_SUCCESS>(handle, sCmd1, sData1);
	}
	makeConnection<ACNET_NLM>("ERROR", sCmd1, sData1);

	// Remove all the task handles we just created.

	disconnect<ACNET_SUCCESS>(sCmd, ator(thValidator));
    }
    std::cerr << "++++ CONNECT command is working\n";
}

static void testRequests() throw(regression)
{
    try {
	// Try to connect as VLDTOR again.

	regression::context = "setting up one-shot request tests";
	makeConnection<ACNET_SUCCESS>(thValidator, sCmd1, sData1);
	makeConnection<ACNET_SUCCESS>("SERVER", sCmd2, sData2);

	regression::context = "sending one-shot request";
	{
	    uint8_t const payload[] = { 0x12, 0x34, 0x56, 0x78 };
	    reqid_t const rid __attribute__((unused)) = sendRequest(sCmd1, "SERVER", 0, false, 1000, payload);
	    AcnetPacket pkt;

	    if (!getPacket<ACNET_SUCCESS>(sData2, pkt, 1000))
		throw regression("timeout waiting for request");
	    if (*(pkt.begin()) != payload[0] ||
		*(pkt.begin() + 1) != payload[1] ||
		*(pkt.begin() + 2) != payload[2] ||
		*(pkt.begin() + 3) != payload[3])
		throw regression("request data is wrong");

	    if (!getPacket<ACNET_TMO>(sData1, pkt, 1500))
		throw regression("expecting ACNET_UTIME, but never saw a packet");
	}

	disconnect<ACNET_SUCCESS>(sCmd1, ator(thValidator));
    }
    catch (...) {
	disconnect<ACNET_SUCCESS>(sCmd1, ator(thValidator));
	throw;
    }
    std::cerr << "++++ REQUEST command is working\n";
}

static void waitForNoResponse(int s, int tmo) throw(regression)
{
    pollfd pfd = { s, POLLIN, 0 };

    int const res = poll(&pfd, 1, tmo);

    switch (res) {
     case -1:
	throw regression("couldn't wait for data on socket -- %d", errno);

     case 0:
	break;

     default:
	throw regression("data available?");
    }
}

template <class T>
static size_t waitForResult(int s, T& buf, int tmo) throw(regression)
{
    return waitForResult(s, &buf, sizeof(T), tmo);
}

static size_t waitForResult(int s, void* buf, size_t n, int tmo) throw(regression)
{
    pollfd pfd = { s, POLLIN, 0 };

    int const res = poll(&pfd, 1, tmo);

    switch (res) {
     case -1:
	throw regression("couldn't wait for data on socket -- %d", errno);

     case 0:
	throw regression("timed out waiting for data from socket");

     default:
	{
	    char tmp[ACNET_PACKET_SIZE];
	    ssize_t const len = recvfrom(s, tmp, sizeof(tmp), 0, 0, 0);

	    if (-1 == len)
		throw regression("recvfrom() returned an error -- %d", errno);
	    memcpy(buf, tmp, std::min((size_t) len, n));
	    return len;
	}
	break;
    }
}

int main(int, char**)
{
    std::cerr << "#### ACNET Validator ####\n";

    try {
	testCommandInterface();
	testConnects();
	testRequests();
	testCleanup();

	// Free up our resources.

	printf("disconnect\n");
	disconnect<ACNET_SUCCESS>(sCmd1, ator(thValidator));
	printf("release sockets\n");
	releaseSockets();
    }
    catch (std::exception const& e) {
	std::cerr << "---- ERROR: (context: " << regression::context << ") " << e.what() << std::endl;
	releaseSockets();
	return 1;
    }

    std::cerr << "++++ ACNET has passed all tests.\n";
    return 0;
}
