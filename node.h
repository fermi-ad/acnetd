#ifndef __NODE_H
#define __NODE_H

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

    void detach()
    {
	prev_->next_ = next_;
	next_->prev_ = prev_;
	next_ = prev_ = this;
    }
};

template <Node& root>
class Element : public Node {
 public:
    void insertBefore(Element* e) { Node::insertBefore(e); }
    void insertAfter(Element* e) { Node::insertBefore(e->next()); }
    void append() { Node::insertBefore(&root); }
    void prepend() { Node::insertBefore(root.next()); }

    static Element* first()
    {
	return dynamic_cast<Element*>(root.next());
    }

    static Element* last()
    {
	return dynamic_cast<Element*>(root.prev());
    }

    Element* next() const
    {
	return dynamic_cast<Element*>(this->Node::next());
    }

    Element* prev() const
    {
	return dynamic_cast<Element*>(this->Node::prev());
    }
};

// Local Variables:
// mode:c++
// End:

#endif
