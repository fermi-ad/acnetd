#ifndef __TRUNKNODE_H
#define __TRUNKNODE_H

#include <inttypes.h>

class trunk_t {
    size_t t_;

 public:
    explicit trunk_t(size_t t) : t_(t) { assert(t < 256); }
    template <class T> trunk_t(T) = delete;
    bool operator< (trunk_t const o) const { return t_ < o.t_; }
    bool operator== (trunk_t const o) const { return t_ == o.t_; }
    bool operator!= (trunk_t const o) const { return t_ != o.t_; }
    uint8_t raw() const { return t_; }
};

class node_t {
    size_t n_;

 public:
    explicit node_t(size_t n) : n_(n) { assert(t < 256); }
    template <class T> node_t(T) = delete;
    bool operator< (node_t const o) const { return n_ < o.n_; }
    bool operator== (node_t const o) const { return n_ == o.n_; }
    bool operator!= (node_t const o) const { return n_ != o.n_; }
    uint8_t raw() const { return n_; }
};

class trunknode_t {
    size_t tn;

 public:
    trunknode_t() : tn(0) {}
    trunknode_t(trunk_t const t, node_t const n) :
	tn((uint16_t(t.raw()) << 8) | uint16_t(n.raw())) {}
    trunknode_t(trunknode_t const &obj) : tn(obj.tn) {}
    explicit trunknode_t(uint16_t const addr) : tn(addr) {}

    bool operator< (trunknode_t const o) const { return tn < o.tn; }
    bool operator== (trunknode_t const o) const { return tn == o.tn; }
    bool operator!= (trunknode_t const o) const { return tn != o.tn; }

    bool isBlank() const { return tn == 0; }
    trunk_t trunk() const { return trunk_t(tn >> 8); }
    uint8_t node() const { return uint8_t(tn); }
    uint16_t raw() const { return tn; }
};

// Local Variables:
// mode:c++
// End:

#endif
