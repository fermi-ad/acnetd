#include "server.h"
#include <sys/socket.h>
#include <inttypes.h>
#include <iterator>

// WebSocket TCP client protocol implementation

#define MAX_PAYLOAD_SIZE	(INTERNAL_ACNET_PACKET_SIZE + 2)

template<typename _Container>
class decode_iterator
    : public std::iterator<std::output_iterator_tag, void, void, void, void>
{
    uint32_t const mask;
    int offset;
    _Container& container;

 public:
    typedef _Container container_type;

    decode_iterator(_Container& __x, uint32_t const __m) :
			mask(__m), offset(3), container(__x) {}

    decode_iterator& operator=(uint8_t const __value)
    {
	container.push_back(__value ^ uint8_t(mask >> (offset * 8)));
	offset = (offset + 3) % sizeof(mask);
	return *this;
    }

    decode_iterator& operator*()
    {
	return *this;
    }

    decode_iterator& operator++()
    {
	return *this;
    }

    decode_iterator operator++(int)
    {
	return *this;
    }
};

template<typename _Container>
inline decode_iterator<_Container> decode_inserter(_Container& __x,
			   uint32_t const __m)
{
    return decode_iterator<_Container>(__x, __m);
}

WebSocketProtocolHandler::WebSocketProtocolHandler(int sTcp, int sCmd, int sData, nodename_t tcpNode, ipaddr_t remoteAddr) :
			    TcpClientProtocolHandler(sTcp, sCmd, sData, tcpNode, remoteAddr),
			    payload(MAX_PAYLOAD_SIZE)
{
    payload.clear();
}

bool WebSocketProtocolHandler::readPayloadLength(uint8_t b, uint64_t& len)
{
    if (b == 127) {
	uint32_t tmp[2];

	if (readBytes(tmp, sizeof(tmp))) {
	    len = (int64_t(ntohl(tmp[0])) << 32) + int64_t(ntohl(tmp[1]));
	    return len <= MAX_PAYLOAD_SIZE;
	} else
	    return false;
    } else if (b == 126) {
	uint16_t tmp;

	if (readBytes(&tmp, sizeof(tmp)))
	    len = uint64_t(ntohs(tmp));
	else
	    return false;
    } else
	len = uint64_t(b);

    return true;
}

bool WebSocketProtocolHandler::readMask(bool msk, uint32_t& mask)
{
    if (msk) {
	if (readBytes(&mask, sizeof(mask)))
	    mask = ntohl(mask);
	else
	    return false;
    } else
	mask = 0;

    return true;
}

bool WebSocketProtocolHandler::sendBinaryDataToClient(Pkt2 *pkt2, ssize_t len, uint16_t type)
{
    bool done = false;

    if (len <= 125) {

	// Convert packet (Pkt2) to small header size (Pkt1) and send

	Pkt1 *pkt1 = (Pkt1 *) (((uint8_t *) pkt2) + (sizeof(Pkt2) - sizeof(Pkt1)));
	pkt1->op = 0x82;
	pkt1->len = len + sizeof(pkt1->type);
	pkt1->type = htons(type);

	if (send(sTcp, pkt1, len + sizeof(Pkt1), 0) == -1) {
	    syslog(LOG_ERR, "wshandler: error sending binary data to client -- %m");
	    done = true;
	}
    } else {

	// Send packet (Pkt2)

	pkt2->op = 0x82;
	pkt2->_126 = 126;
	pkt2->len = htons(len + sizeof(pkt2->type));
	pkt2->type = htons(type);

	if (send(sTcp, pkt2, len + sizeof(Pkt2), 0) == -1) {
	    syslog(LOG_ERR, "wshandler: error sending binary data to client -- %m");
	    done = true;
	}
    }

    return done;
}

bool WebSocketProtocolHandler::handleDataSocket()
{
    bool done = false;
    uint8_t buf[64 * 1024];
    Pkt2 *pkt2 = (Pkt2 *) buf;

    ssize_t const len = recv(sData, pkt2->data, sizeof(buf) - sizeof(Pkt2), 0);

    if (len < 0) {
	syslog(LOG_ERR, "tcpclient: error receiving data -- %m");
	done = true;
    } else
	done = sendBinaryDataToClient(pkt2, len, ACNETD_DATA);

    return done;
}

bool WebSocketProtocolHandler::handleAcnetCommand(std::vector<uint8_t>& payload)
{
    bool done = false;
    uint16_t type;

    if (payload.size() >= sizeof(type)) {
	type = ntohs(*(uint16_t *) payload.data());
	uint16_t len = (uint16_t) payload.size() - sizeof(type);

	if (type == ACNETD_COMMAND && len >= sizeof(CommandHeader))
	    done = handleClientCommand((CommandHeader *) (payload.data() + sizeof(type)), len);
	else
	    done = true;
    } else
	done = true;

    return done;
}

bool WebSocketProtocolHandler::handleClientSocket()
{
    bool done = false;
    uint8_t hdr[2];

    if (readBytes(hdr, sizeof(hdr))) {
	uint32_t mask;
	uint64_t len;
	bool hasMask = hdr[1] & 0x80;

	if (readPayloadLength(hdr[1] & 0x7f, len) && readMask(hasMask, mask)) {
	    uint8_t buf[MAX_PAYLOAD_SIZE];

	    if (readBytes(buf, len)) {

		// Save next frame of payload

		if (hasMask)
		    std::copy(buf, buf + len, decode_inserter(payload, mask));
		else
		    std::copy(buf, buf + len, std::back_inserter(payload));

		// If final fragment, then send the payload on

		if (hdr[0] & 0x80) {

		    // Check message opcode

		    switch (hdr[0] & 0x0f) {
		     case 0x1:
			{
			    std::string s((char *) payload.data(), payload.size());
			    done = handleAcnetCommand(payload);
			}
			break;

		     case 0x2:
			done = handleAcnetCommand(payload);
			break;

		     case 0x8:
			{
			    uint8_t msg[] = { 0x88, (uint8_t) payload.size() };
			    send(sTcp, msg, sizeof(msg), 0);
			    send(sTcp, payload.data(), payload.size(), 0);
			    done = true;
			}
			break;

		     case 0x9:
			{
			    uint8_t pong[] = { 0x8a, (uint8_t) payload.size() };
			    send(sTcp, pong, sizeof(pong), 0);
			    send(sTcp, payload.data(), payload.size(), 0);
			}
			break;

		     case 0xa:
			break;
		    }

		    payload.clear();
		}
	    } else
		done = true;
	} else
	    done = true;
    } else
	done = true;

    return done;
}

bool WebSocketProtocolHandler::handleCommandSocket()
{
    bool done = false;
    uint8_t buf[64 * 1024];
    Pkt2 *pkt2 = (Pkt2 *) buf;

    ssize_t const len = recv(sCmd, pkt2->data, sizeof(buf) - sizeof(Pkt2), 0);

    if (len < 0) {
	syslog(LOG_ERR, "tcpclient: error receiving command ack -- %m");
	done = true;
    } else
	done = sendBinaryDataToClient(pkt2, len, ACNETD_ACK);

    return done;
}

bool WebSocketProtocolHandler::handleClientPing()
{
    uint8_t ping[] = { 0x89, 0x00 };

    if (send(sTcp, ping, sizeof(ping), 0) == -1) {
	syslog(LOG_ERR, "wshandler: error sending ping to client -- %m");
	return true;
    }

    return false;
}

void WebSocketProtocolHandler::handleShutdown()
{
    uint8_t msg[] = { 0x88, 0x00 };

    if (send(sTcp, msg, sizeof(msg), 0) == -1)
	syslog(LOG_ERR, "wshandler: error sending close to client -- %m");
}

