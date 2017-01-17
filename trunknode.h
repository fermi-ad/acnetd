#ifndef __TRUNKNODE_H
#define __TRUNKNODE_H

#include <inttypes.h>

class trunknode_t {
    uint16_t tn;

 public:
    trunknode_t() : tn(0) {}
    trunknode_t(uint8_t const t, uint8_t const n) :
	tn((uint16_t(t) << 8) | uint16_t(n)) {}
    trunknode_t(trunknode_t const &obj) : tn(obj.tn) {}
    explicit trunknode_t(uint16_t const addr) : tn(addr) {}

    bool operator< (trunknode_t const o) const { return tn < o.tn; }
    bool operator== (trunknode_t const o) const { return tn == o.tn; }
    bool operator!= (trunknode_t const o) const { return tn != o.tn; }

    bool isBlank() const { return tn == 0; }
    uint8_t trunk() const { return uint8_t(tn >> 8); }
    uint8_t node() const { return uint8_t(tn); }
    uint16_t raw() const { return tn; }
};

#endif
