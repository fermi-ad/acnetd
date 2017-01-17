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
