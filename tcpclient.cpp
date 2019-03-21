#include <cstring>
#include "server.h"
#include <sys/socket.h>
#include <signal.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <sha.h>

#define SEND_BUF_SIZE                   (64 * 1024 - 2)
#define RECEIVE_BUF_SIZE                (128 * 1024)


TcpClientProtocolHandler::TcpClientProtocolHandler(int sTcp, int sCmd, int sData,
						   nodename_t tcpNode, ipaddr_t remoteAddr) :
    sTcp(sTcp), sCmd(sCmd), sData(sData), tcpNode(tcpNode),
    remoteAddr(remoteAddr), enabledTraffic(AllTraffic)
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
	else {
	    syslog(LOG_INFO, "tcpclient: error reading from the client -- %m");
	    return false;
	}
    }

    return true;
}

int TcpClientProtocolHandler::getSocketPort(int s)
{
    struct sockaddr_in in;
    socklen_t in_len = (socklen_t) sizeof(in);

    if (-1 == getsockname(s, (struct sockaddr*) &in, &in_len)) {
	syslog(LOG_ERR, "tcpclient: unable to read data socket port -- %m");
	return -1;
    } else
	return ntohs(in.sin_port);
}

bool TcpClientProtocolHandler::handleClientCommand(CommandHeader *cmd, size_t len)
{
    int res = -1;

    switch (cmd->cmd()) {
     case CommandList::cmdConnect:
     case CommandList::cmdTcpConnect:
	{
	    TcpConnectCommand tmp;

	    tmp.setClientName(cmd->clientName());
	    tmp.setVirtualNodeName(tcpNode);
	    tmp.setPid(getpid());
	    tmp.setDataPort(getSocketPort(sData));
	    tmp.setRemoteAddr(remoteAddr);

	    res = send(sCmd, &tmp, sizeof(tmp), 0);
	}
	break;

     default:
	res = send(sCmd, cmd, len, 0);
	break;
    }

    if (res == -1) {
	syslog(LOG_ERR, "tcpclient: error sending command to acnetd -- %m");
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

	int v = SEND_BUF_SIZE;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "tcpclient: couldn't set data socket send buffer size -- %m");
	v = RECEIVE_BUF_SIZE;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "tcpclient: couldn't set data socket receive buffer size -- %m");

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
                syslog(LOG_ERR, "tcpclient: couldn't connect data socket to acnetd -- %m");
        } else {
            socklen_t in_len = (socklen_t) sizeof(in);

            getsockname(s, (struct sockaddr*) &in, &in_len);
            syslog(LOG_ERR, "tcpclient: couldn't bind to localhost:%d (data) -- %m", ntohs(in.sin_port));
        }
        close(s);
    } else
        syslog(LOG_ERR, "tcpclient: couldn't open AF_INET acnet client data socket -- %m");

    return -1;
}

static int createCommandSocket()
{
    int s;

    if (-1 != (s = socket(AF_INET, SOCK_DGRAM, 0))) {
        struct sockaddr_in in;

	int v = SEND_BUF_SIZE;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "tcpclient: couldn't set command socket send buffer size -- %m");
	v = RECEIVE_BUF_SIZE;
        if (-1 == setsockopt(s, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)))
            syslog(LOG_ERR, "tcpclient: couldn't set command socket receive buffer size -- %m");

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
            if (errno == EINTR)
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

static void sendStr(int s, std::string const& str)
{
    send(s, str.c_str(), str.size(), 0);
}

static std::string getAcceptKey(char const* const key)
{
    static char const encodingTable[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static int const modTable[] = {0, 2, 1};
    static char const magicKey[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    SHA_CTX ctx;
    unsigned char md[SHA_DIGEST_LENGTH];

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, key, strlen(key));
    SHA1_Update(&ctx, magicKey, strlen(magicKey));
    SHA1_Final(md, &ctx);

    std::string acceptKey;

    acceptKey.reserve(4 * ((SHA_DIGEST_LENGTH + 2) / 3));

    for (size_t ii = 0; ii < SHA_DIGEST_LENGTH;) {
        uint32_t const octet_a = ii < SHA_DIGEST_LENGTH ? md[ii++] : 0;
        uint32_t const octet_b = ii < SHA_DIGEST_LENGTH ? md[ii++] : 0;
        uint32_t const octet_c = ii < SHA_DIGEST_LENGTH ? md[ii++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        acceptKey += encodingTable[(triple >> 3 * 6) & 0x3F];
        acceptKey += encodingTable[(triple >> 2 * 6) & 0x3F];
        acceptKey += encodingTable[(triple >> 1 * 6) & 0x3F];
        acceptKey += encodingTable[(triple >> 0 * 6) & 0x3F];
    }

    for (int ii = 0; ii < modTable[SHA_DIGEST_LENGTH % 3]; ii++)
        acceptKey += '=';

    return acceptKey;
}

static TcpClientProtocolHandler *handshake(int sTcp, int sCmd, int sData, nodename_t tcpNode, ipaddr_t remoteAddr)
{
    TcpClientProtocolHandler *handler = 0;
    ssize_t len;
    char line[16 * 1024];

    syslog(LOG_DEBUG, "handshake");

    std::string response;

    // Handshake continues until we get a blank line

    while ((len = readHttpLine(sTcp, line, sizeof(line))) > 0) {
	char *cp;

	if (strcmp("RAW", line) == 0) {
	    handler = new RawProtocolHandler(sTcp, sCmd, sData, tcpNode,
					     remoteAddr);
	    syslog(LOG_DEBUG, "detected Raw protocol");
	} else if (!handler && (cp = strchr(line, ' '))) {
	    *cp++ = 0;
	    if (strcmp("Sec-WebSocket-Key:", line) == 0) {
		static const std::string header =
		    "HTTP/1.1 101 Switching Protocols\r\n"
		    "Upgrade: websocket\r\n"
		    "Connection: Upgrade\r\n"
		    "Sec-WebSocket-Accept: ";

		response += header + getAcceptKey(cp) + "\r\n";
		handler = new WebSocketProtocolHandler(sTcp, sCmd, sData,
						       tcpNode, remoteAddr);
		syslog(LOG_DEBUG, "detected WebSocket protocol");
	    } else if (strcmp("Sec-WebSocket-Protocol:", line) == 0)
		response += "Sec-WebSocket-Protocol: acnet-client\r\n";
	}
    }
    sendStr(sTcp, response += "\r\n");
    return handler;
}

bool TcpClientProtocolHandler::commandSocketData()
{
    enabledTraffic = AllTraffic;
    return this->handleCommandSocket();
}

void handleTcpClient(int sTcp, nodename_t tcpNode)
{
    bool done = false;

    // Ignore SIGPIPE so we get socket errors when we try to
    // write to the client TCP socket

    signal(SIGPIPE, SIG_IGN);

    // Setup the client TCP socket

    int v = 1;
    if (-1 == setsockopt(sTcp, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)))
	syslog(LOG_WARNING, "tcpclient: couldn't set TCP_NODELAY for socket -- %m");

    v = SEND_BUF_SIZE;
    if (-1 == setsockopt(sTcp, SOL_SOCKET, SO_SNDBUF, &v, sizeof(v)))
	syslog(LOG_ERR, "tcpclient: couldn't set data socket send buffer size -- %m");

    struct timeval tv = { 3, 0 };
    if (-1 == setsockopt(sTcp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
	syslog(LOG_ERR, "tcpclient: couldn't set data socket timeout -- %m");

    // Connect to acnetd

    int sCmd = createCommandSocket();
    int sData = createDataSocket();

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    ipaddr_t ip;
    if (getpeername(sTcp, (sockaddr*) &addr, &len) == 0) {
	ip = ipaddr_t(ntohl(addr.sin_addr.s_addr));
	syslog(LOG_NOTICE, "tcpclient: connection from host %s", ip.str().c_str());
    } else
	syslog(LOG_NOTICE, "tcpclient: connection on socket: %d", sTcp);

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

	    int const n = handler->whichTraffic() == TcpClientProtocolHandler::AckTraffic ? 1 : sizeof(pfd) / sizeof(pfd[0]);
	    int const pollStat = poll(pfd, n, 10000);

	    if (termSignal) {
		handler->handleShutdown();
		done = true;
		continue;
	    }

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
    } else
	syslog(LOG_ERR, "tcpclient: unable to create acnetd connection sockets");

    if (sCmd != -1)
	close(sCmd);

    if (sData != -1)
	close(sData);

    close(sTcp);

    exit(1);
}
