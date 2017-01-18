#include <cstring>
#include "server.h"

// Max chars for rad50 ASCII string

#define RAD50_ASCII_COUNT	(6)

// Rad50 char table
static char const rad50Char[] = {
	' ', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',
	'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S',
	'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '$', '.', '%',
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'
};

// Convert a single char to an index into the
// rad50 char table

static uint16_t charToIndex(char c)
{
    if (c >= 'A' && c <= 'Z')
	return c - 'A' + 1;
    else if (c >= 'a' && c <= 'z')
	return c - 'a' + 1;
    else if (c >= '0' && c <= '9')
	return c - '0' + 30;
    else if (c == '$')
	return 27;
    else if (c == '.')
	return 28;
    else if (c == '%')
	return 29;

    return 0;
}

// Convert ASCII string to rad50

uint32_t ator(char const *s)
{
    uint16_t v1 = 0, v2 = 0;
    int len = strlen(s);

    for (int ii = 0; ii < RAD50_ASCII_COUNT; ii++) {
	char c = (ii < len ? s[ii] : ' ');

	if (ii < (RAD50_ASCII_COUNT / 2)) {
	    v1 *= 40;
	    v1 += charToIndex(c);
	} else {
	    v2 *= 40;
	    v2 += charToIndex(c);
	}
    }

    return (v2 << 16) | v1;
}

extern "C" uint32_t rad50(char* s)
{
    return ator(s);
}

uint32_t jradix50(char* s)
{
    return ator(s);
}

// Convert rad50 to ASCII string
// Use internal buffer if s is 0

char const* rtoa(uint32_t r, char *s)
{
    static char buf[RAD50_ASCII_COUNT + 1];

    if (s == 0)
	s = buf;

    int v1 = r & 0xffff;
    int v2 = (r >> 16) & 0xffff;

    for (int ii = 0; ii < (RAD50_ASCII_COUNT / 2); ii++) {
	*(s + (RAD50_ASCII_COUNT / 2) - ii - 1) = rad50Char[v1 % 40];
	v1 /= 40;
	*(s + RAD50_ASCII_COUNT - ii - 1) = rad50Char[v2 % 40];
	v2 /= 40;
    }

    *(s + RAD50_ASCII_COUNT) = 0;

    return s;
}

char const* rtoa_strip(uint32_t r, char *s)
{
    static char buf[RAD50_ASCII_COUNT + 1];

    if (s == 0)
	s = buf;

    rtoa(r, s);

    char *c = strchr(s, ' ');
    if (c)
	*c = 0;

    return s;
}

// Local Variables:
// mode:c++
// fill-column:125
// End:
