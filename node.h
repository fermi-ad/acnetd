#include <time.h>
#include <sys/time.h>

class Node {
    Node* next_;
    Node* prev_;

 protected:
    void insertBefore(Node*);

 public:
    Node();
    virtual ~Node();

    Node* next() const { return next_; }
    Node* prev() const { return prev_; }
    void detach();
    virtual void update(Node*);
};

timeval const& now();
timeval operator+(timeval const&, unsigned);
bool operator<=(timeval const&, timeval const&);

struct TimeSensitive : public Node {
    timeval lastUpdate;

    TimeSensitive();
    void update(Node*);
    virtual timeval expiration() const = 0;
};
