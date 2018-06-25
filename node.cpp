#include "node.h"

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

