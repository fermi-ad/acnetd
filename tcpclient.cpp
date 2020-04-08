#include <cstring>
#include "server.h"
#include <sys/socket.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <sha.h>

TcpClientProtocolHandler::TcpClientProtocolHandler(int sTcp, int sCmd, int sData,
						   nodename_t tcpNode) :
    maxSocketQSize(0), sTcp(sTcp), sCmd(sCmd), sData(sData), tcpNode(tcpNode),
	enabledTraffic(AllTraffic)
{
}

bool TcpClientProtocolHandler::readBytes(void *buf, size_t count)
{
    void * const end = ((uint8_t*) buf) + count;

    while (buf < end) {
	ssize_t const len = recv(sTcp, buf, (int8_t *) end - (int8_t *) buf, 0);

	if (len > 0)
	    buf = ((uint8_t*) buf) + len;
	else if (len == 0)
	    return false;
	else if (len == -1) {
	    if (errno == EAGAIN || errno == EWOULDBLOCK) {
		poll(0, 0, 1);
	    } else {
		syslog(LOG_ERR, "error reading from the client -- %m");
		return false;
	    }
	}
    }

    return true;
}

int TcpClientProtocolHandler::getSocketPort(int s)
{
    struct sockaddr_in in;
    socklen_t in_len = (socklen_t) sizeof(in);

    if (-1 == getsockname(s, (struct sockaddr*) &in, &in_len)) {
	syslog(LOG_ERR, "unable to read data socket port -- %m");
	return -1;
    } else
	return ntohs(in.sin_port);
}

bool TcpClientProtocolHandler::handleClientCommand(CommandHeader *cmd, size_t len)
{
    int res = -1;

    // If a virtual node is not specified, then use the node that was
    // provided on the -t command line option

    if (cmd->virtualNodeName().isBlank())
	cmd->setVirtualNodeName(tcpNode);

    switch (cmd->cmd()) {
     case CommandList::cmdConnect:
     case CommandList::cmdTcpConnect:
	{
	    TcpConnectCommand tmp;

	    tmp.setClientName(cmd->clientName());
	    tmp.setVirtualNodeName(cmd->virtualNodeName());
	    tmp.setPid(getpid());
	    tmp.setDataPort(getSocketPort(sData));
	    tmp.setRemoteAddr(remoteAddr);

	    res = ::send(sCmd, &tmp, sizeof(tmp), 0);
	}
	break;

     case CommandList::cmdConnectExt:
     case CommandList::cmdTcpConnectExt:
	{
	    TcpConnectCommandExt tmp;

	    tmp.setClientName(cmd->clientName());
	    tmp.setVirtualNodeName(cmd->virtualNodeName());
	    tmp.setPid(getpid());
	    tmp.setDataPort(getSocketPort(sData));
	    tmp.setRemoteAddr(remoteAddr);

	    res = ::send(sCmd, &tmp, sizeof(tmp), 0);
	}
	break;

     default:
	res = ::send(sCmd, cmd, len, 0);
	break;
    }

    if (res == -1) {
	syslog(LOG_ERR, "error sending command to acnetd -- %m");
	return true;
    }

    enabledTraffic = AckTraffic;
    return false;
}

static int createDataSocket()
{
    int s;

    if (-1 != (s = socket(AF_INET, SOCK_DGRAM, 0))) {
        struct sockaddr_in in;

	int v = 256 * 1024;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "couldn't set data socket receive buffer size -- %m");

        in.sin_family = AF_INET;
#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
        in.sin_len = sizeof(in);
#endif

#if THIS_TARGET == Darwin_Target
        in.sin_addr.s_addr = htonl(INADDR_ANY);
#else
        in.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
        in.sin_port = 0;

        if (-1 != ::bind(s, (struct sockaddr*) &in, sizeof(in))) {
            in.sin_family = AF_INET;
#if THIS_TARGET == Darwin_Target
            in.sin_addr.s_addr = htonl(INADDR_ANY);
#else
            in.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
            in.sin_port = htons(ACNET_CLIENT_PORT);

            if (-1 != connect(s, (struct sockaddr*) &in, sizeof(in)))
                return s;
            else
                syslog(LOG_ERR, "couldn't connect data socket to acnetd -- %m");
        } else {
            socklen_t in_len = (socklen_t) sizeof(in);

            getsockname(s, (struct sockaddr*) &in, &in_len);
            syslog(LOG_ERR, "couldn't bind to localhost:%d (data) -- %m", ntohs(in.sin_port));
        }
        close(s);
    } else
        syslog(LOG_ERR, "couldn't open AF_INET acnet client data socket -- %m");

    return -1;
}

static int createCommandSocket()
{
    int s;

    if (-1 != (s = socket(AF_INET, SOCK_DGRAM, 0))) {
        struct sockaddr_in in;

	int v = 64 * 1024;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "couldn't set command socket send buffer size -- %m");

	v = 128 * 1024;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "couldn't set command socket receive buffer size -- %m");

        in.sin_family = AF_INET;
#if THIS_TARGET != Linux_Target && THIS_TARGET != SunOS_Target
        in.sin_len = sizeof(in);
#endif

#if THIS_TARGET == Darwin_Target
        in.sin_addr.s_addr = htonl(INADDR_ANY);
#else
        in.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
        in.sin_port = 0;

        if (-1 != ::bind(s, (struct sockaddr*) &in, sizeof(in))) {
            in.sin_family = AF_INET;
#if THIS_TARGET == Darwin_Target
            in.sin_addr.s_addr = htonl(INADDR_ANY);
#else
            in.sin_addr.s_addr = htonl(INADDR_ANY);
#endif
            in.sin_port = htons(ACNET_CLIENT_PORT);

            if (-1 != connect(s, (struct sockaddr*) &in, sizeof(in)))
                return s;
        }

        // Failed at this point so close the socket

        close(s);
    }

    return -1;
}

static ssize_t readHttpLine(int fd, void *buf, size_t n)
{
    ssize_t numRead;
    size_t totRead = 0;
    char *cp = (char *) buf;
    char ch;

    while (true) {
        numRead = read(fd, &ch, 1);

        if (numRead == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            else
                return -1;
        } else if (numRead == 0) {
            if (totRead == 0)
                return 0;
            else
                break;
        } else {
            if (totRead < n - 1) {
		if (ch != '\r' && ch != '\n') {
		    totRead++;
		    *cp++ = ch;
		}
            }

            if (ch == '\n')
                break;
        }
    }

    *cp = 0;

    return totRead;
}

static void sendStr(int s, const char *d)
{
    send(s, d, strlen(d), 0);
}

static void sendAcceptKey(int sTcp, const char *key)
{
    static char encodingTable[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
				    'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
				    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
				    'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
				    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
				    'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
				    'w', 'x', 'y', 'z', '0', '1', '2', '3',
				    '4', '5', '6', '7', '8', '9', '+', '/'};
    static int modTable[] = {0, 2, 1};
    const char *magicKey = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA_CTX ctx;
    unsigned char md[SHA_DIGEST_LENGTH];

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, key, strlen(key));
    SHA1_Update(&ctx, magicKey, strlen(magicKey));
    SHA1_Final(md, &ctx);

    char acceptKey[4 * ((SHA_DIGEST_LENGTH + 2) / 3)];

    for (size_t ii = 0, jj = 0; ii < SHA_DIGEST_LENGTH;) {

        uint32_t octet_a = ii < SHA_DIGEST_LENGTH ? md[ii++] : 0;
        uint32_t octet_b = ii < SHA_DIGEST_LENGTH ? md[ii++] : 0;
        uint32_t octet_c = ii < SHA_DIGEST_LENGTH ? md[ii++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        acceptKey[jj++] = encodingTable[(triple >> 3 * 6) & 0x3F];
        acceptKey[jj++] = encodingTable[(triple >> 2 * 6) & 0x3F];
        acceptKey[jj++] = encodingTable[(triple >> 1 * 6) & 0x3F];
        acceptKey[jj++] = encodingTable[(triple >> 0 * 6) & 0x3F];
    }

    for (int ii = 0; ii < modTable[SHA_DIGEST_LENGTH % 3]; ii++)
        acceptKey[sizeof(acceptKey) - 1 - ii] = '=';

    send(sTcp, acceptKey, sizeof(acceptKey), 0);
}

static TcpClientProtocolHandler *handshake(int sTcp, int sCmd, int sData, nodename_t tcpNode, ipaddr_t remoteAddr)
{
    TcpClientProtocolHandler *handler = 0;
    ssize_t len;
    char line[16 * 1024];
    pollfd pfd[] = {{ sTcp, POLLIN, 0 }};

    if (poll(pfd, 1, 100) > 0) {

	// Handshake continues until we get a blank line

	while ((len = readHttpLine(sTcp, line, sizeof(line))) > 0) {
	    char *cp;

	    if (strcmp("RAW", line) == 0) {
		handler = new RawProtocolHandler(sTcp, sCmd, sData, tcpNode);
		syslog(LOG_NOTICE, "detected raw protocol");
	    } else if ((cp = strchr(line, ' '))) {
		*cp++ = 0;

		if (!handler) {
		    if (strcasecmp("Sec-WebSocket-Key:", line) == 0) {
			sendStr(sTcp, "HTTP/1.1 101 Switching Protocols\r\n");
			sendStr(sTcp, "Upgrade: websocket\r\n");
			sendStr(sTcp, "Connection: Upgrade\r\n");
			sendStr(sTcp, "Sec-WebSocket-Accept: ");
			sendAcceptKey(sTcp, cp);
			sendStr(sTcp, "\r\n");
			sendStr(sTcp, "Sec-WebSocket-Protocol: acnet-client\r\n\r\n");
			handler = new WebSocketProtocolHandler(sTcp, sCmd, sData, tcpNode);
			syslog(LOG_DEBUG, "detected websocket protocol");
		    }
		}

		// Check for proxied connections and report the forwarded address on the connection

		if (strcmp("X-Forwarded-For:", line) == 0) {
		    struct in_addr addr;

		    if (inet_pton(AF_INET, cp, &addr) == 1) {
			remoteAddr = ipaddr_t(ntohl(addr.s_addr));
			syslog(LOG_NOTICE, "detected proxy forward address: %s", remoteAddr.str().c_str());
		    }
		}
	    }
	}

	if (handler)
	    handler->setRemoteAddress(remoteAddr);
    }

    return handler;
}

bool TcpClientProtocolHandler::commandSocketData()
{
    enabledTraffic = AllTraffic;
    return this->handleCommandSocket();
}

static inline bool socketBufferFull()
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EMSGSIZE;
}

bool TcpClientProtocolHandler::send(const void *buf, const size_t len)
{
    if (socketQ.empty()) {
	ssize_t sLen = ::send(sTcp, buf, len, 0);

	if (-1 == sLen) {
	    if (!socketBufferFull())
		return false;
	    sLen = 0;
	}

	if ((size_t) sLen < len)
	    socketQ.push(new SocketBuffer(((uint8_t *) buf) + sLen, len - sLen));

    } else {
	SocketBuffer *sBuf = socketQ.current()->append(buf, len);

	if (sBuf)
	    socketQ.push(sBuf);
    }

    if (socketQ.size() > maxSocketQSize)
	maxSocketQSize = socketQ.size();
		
    return true;
}

bool TcpClientProtocolHandler::sendPendingPackets()
{
    while (!socketQ.empty()) {
	SocketBuffer* const sBuf = socketQ.peek();

	ssize_t const remaining = sBuf->remaining();
	ssize_t const sLen = ::send(sTcp, sBuf->data(), remaining, 0);

	if (-1 == sLen) {
	    if (socketBufferFull()) {
		return false;
	    } else {
		syslog(LOG_ERR, "couldn't send packet to socket -- %m");
		return true;
	    }
	}

	sBuf->consume(sLen);

	if (sBuf->empty())
	    delete socketQ.pop();

	if (sLen < remaining)
	    return false;
    }

    return false;
}

void handleTcpClient(int sTcp, nodename_t tcpNode)
{
    bool done = false;
 
    openlog("acnetd-tcp-fork", LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    // Ignore SIGPIPE so we get socket errors when we try to
    // write to the client TCP socket

    signal(SIGPIPE, SIG_IGN);

    // Setup the client TCP socket

    int v = 1;
    if (-1 == setsockopt(sTcp, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)))
	syslog(LOG_WARNING, "couldn't set TCP_NODELAY for socket -- %m");

    v =  256 * 1024;
    if (-1 == setsockopt(sTcp, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)))
	syslog(LOG_ERR, "couldn't set data socket send buffer size -- %m");

    struct timeval tv = { 3, 0 };
    if (-1 == setsockopt(sTcp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
	syslog(LOG_ERR, "couldn't set data socket timeout -- %m");

    if (-1 == fcntl(sTcp, F_SETFL, O_NONBLOCK))
	syslog(LOG_ERR, "unable to set socket non-blocking -- %m");

    // Connect to acnetd

    int sCmd = createCommandSocket();
    int sData = createDataSocket();

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    ipaddr_t ip;
    if (getpeername(sTcp, (sockaddr*) &addr, &len) == 0) {
	ip = ipaddr_t(ntohl(addr.sin_addr.s_addr));
	syslog(LOG_NOTICE, "connection from host %s", ip.str().c_str());
    } else
	syslog(LOG_NOTICE, "connection on socket: %d", sTcp);

    if (sCmd != -1 && sData != -1) {
	TcpClientProtocolHandler *handler = handshake(sTcp, sCmd, sData, tcpNode, ip);

	if (!handler) {
	    close(sTcp);
	    close(sCmd);
	    close(sData);
	    exit(1);
	}

	// Handle TCP client and acnetd messages

	while (!done) {
	    pollfd pfd[] = {
		{ sCmd, POLLIN, 0 },
		{ sTcp, POLLIN, 0 },
		{ sData, POLLIN, 0 }
	    };

	    if (handler->anyPendingPackets())
		pfd[1].events |= POLLOUT;

	    int const n = handler->whichTraffic() == TcpClientProtocolHandler::AckTraffic ? 1 : sizeof(pfd) / sizeof(pfd[0]);
	    int const pollStat = poll(pfd, n, 10000);

	    if (termSignal) {
		handler->handleShutdown();
		done = true;
		continue;
	    }

	    if (pfd[1].revents & POLLOUT)
		done = handler->sendPendingPackets();

	    if (!done) {
		if (pollStat > 0) {

		    // Check data socket from acnetd

		    if (pfd[2].revents & POLLIN)
			done = handler->handleDataSocket();

		    // Check for acnetd commands from TCP client

		    if (pfd[1].revents & POLLIN)
			done = handler->handleClientSocket();

		    // Check for command acks from acnetd

		    if (pfd[0].revents & POLLIN)
			done = handler->commandSocketData();

		} else if (pollStat == 0) {
		    if (0 != kill(getppid(), 0) && errno == ESRCH)
			done = true;
		    else
			done = handler->handleClientPing();
		}
	    }

	    if (handler->queueSize() > MAX_SOCKET_QUESIZE) {
		syslog(LOG_ERR, "disconnecting slow client from host %s", handler->remoteAddress().str().c_str());
		done = true;
	    }
	}

	syslog(LOG_ERR, "disconnect from host %s (max queue size %ld)", 
				handler->remoteAddress().str().c_str(), handler->maxQueueSize());

    } else
	syslog(LOG_ERR, "unable to create acnetd connection sockets");

    if (sCmd != -1)
	close(sCmd);

    if (sData != -1)
	close(sData);

    close(sTcp);

    exit(1);
}
