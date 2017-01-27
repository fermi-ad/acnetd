#include "server.h"
#include <sys/socket.h>

// RAW TCP client protocol implementation

template <unsigned Typ>
struct TcpHeader {
    uint32_t size;
    uint16_t type;

    TcpHeader() : type(htons(Typ)) {}
} __attribute__((packed));

template <unsigned Typ>
struct TcpBuffer {
    TcpHeader<Typ> hdr;
    uint8_t data[64 * 1024];
} __attribute__((packed));

template <class T, unsigned Typ>
struct TypedTcpBuffer : public TcpBuffer<Typ> {
    TypedTcpBuffer()
    {
	new (TcpBuffer<Typ>::data)T();
	this->hdr.size = htonl(sizeof(T) + sizeof(this->hdr.type));
    }

    ~TypedTcpBuffer() { reinterpret_cast<T*>(this->data)->~T(); }

    size_t length() const { return sizeof(this->hdr) + sizeof(T); }
    T const& get() const { return reinterpret_cast<T&>(this->data); }
    T& get() { return reinterpret_cast<T&>(this->data); }
};

RawProtocolHandler::RawProtocolHandler(int sTcp, int sCmd, int sData, nodename_t tcpNode, ipaddr_t remoteAddr) :
					TcpClientProtocolHandler(sTcp, sCmd, sData, tcpNode, remoteAddr)
{
}

bool RawProtocolHandler::handleDataSocket()
{
    bool done = false;
    TcpBuffer<ACNETD_DATA> data;
    ssize_t const len = recv(sData, data.data, sizeof(data.data), 0);

    // Test for errors and correct length.

    if (len < 0) {
	syslog(LOG_ERR, "tcpclient: error receiving data from acnetd -- %m");
	done = true;
    } else {
	data.hdr.size = htonl(len + sizeof(data.hdr.type));
	if (send(sTcp, &data, len + sizeof(data.hdr), 0) == -1) {
	    syslog(LOG_ERR, "tcpclient: error sending data to client -- %m");
	    done = true;
	}
    }

    return done;
}

bool RawProtocolHandler::handleClientSocket()
{
    bool done = false;
    TcpBuffer<ACNETD_COMMAND> buf;

    // Read the size

    if (readBytes(&buf.hdr.size, sizeof(buf.hdr.size))) {
	buf.hdr.size = ntohl(buf.hdr.size);

	// If the packet size is within the buffer limits then read the packet size in

	if (buf.hdr.size <= (sizeof(buf.data) + sizeof(buf.hdr.type))) {
	    if (readBytes(&buf.hdr.type, buf.hdr.size)) {
		size_t len = buf.hdr.size - sizeof(buf.hdr.type);

		// If it's an ACNETD_COMMAND and at least the the size of command header then
		// check for certain commands

		if (ntohs(buf.hdr.type) == ACNETD_COMMAND && len >= sizeof(CommandHeader))
		    done = handleClientCommand((CommandHeader *) buf.data, len);
		else {
		    syslog(LOG_ERR, "tcpclient: invalid command/size from client");
		    done = true;
		}
	    } else {
		syslog(LOG_INFO, "tcpclient: unable to read %d bytes from client", buf.hdr.size);
		done = true;
	    }
	} else {
	    syslog(LOG_ERR, "tcpclient: invalid packet size %d from client", buf.hdr.size);
	    //done = true;
	}
    } else
	done = true;

    return done;
}

bool RawProtocolHandler::handleCommandSocket()
{
    bool done = false;
    TcpBuffer<ACNETD_ACK> ack;
    ssize_t const len = recv(sCmd, ack.data, sizeof(ack.data), 0);

    // Test for errors and correct length.

    if (len < 0) {
	syslog(LOG_ERR, "tcpclient: error receiving command ack -- %m");
	done = true;
    } else {
	ack.hdr.size = htonl(len + sizeof(ack.hdr.type));
	if (send(sTcp, &ack, len + sizeof(ack.hdr), 0) == -1) {
	    syslog(LOG_ERR, "tcpclient: error sending command ack -- %m");
	    done = true;
	}
    }

    return done;
}

bool RawProtocolHandler::handleClientPing()
{
    TcpHeader<TCP_CLIENT_PING> png;

    png.size = htonl(sizeof(png.type));
    if (send(sTcp, &png, sizeof(png), 0) == -1) {
	syslog(LOG_ERR, "tcpclient: error pinging client -- %m");
	return true;
    }

    return false;
}

void RawProtocolHandler::handleShutdown()
{
}

