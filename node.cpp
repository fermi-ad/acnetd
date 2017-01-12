#include "server.h"

Node::Node() :
    next_(this), prev_(this)
{
}

Node::~Node()
{
    detach();
}

void Node::detach()
{
    prev_->next_ = next_;
    next_->prev_ = prev_;
    next_ = prev_ = this;
}

void Node::insertBefore(Node* const node)
{
    prev_ = (next_ = node)->prev_;
    next_->prev_ = prev_->next_ = this;
}

void Node::update(Node* const root)
{
    detach();
    insertBefore(root);
}

TimeSensitive::TimeSensitive()
{
    lastUpdate.tv_sec = lastUpdate.tv_usec = 0;
}

void TimeSensitive::update(Node* const root)
{
    detach();
    lastUpdate = now();

    timeval const ourExp = expiration();
    Node* current = root;

    while ((current = current->prev()) != root)
	if (dynamic_cast<TimeSensitive const*>(current)->expiration() <= ourExp) {
	    current = current->next();
	    break;
	}
    insertBefore(current);
}
