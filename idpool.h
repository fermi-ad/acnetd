#ifndef __IDPOOL_H
#define __IDPOOL_H

#include <cassert>
#include <cstdlib>
#include <bitset>

// Template to create pool of objects referenced by id. The size of the pool needs to be a power
// of two.

template<class T, class R, size_t size>
class IdPool {

    class CircBuf {
	size_t item[size];
	size_t nItems, head;

     public:
	CircBuf() : nItems(0), head(0) {}

	size_t total() const { return nItems; }

	void push(size_t id)
	{
	    assert(nItems < size);
	    item[(head + nItems++) % size] = id;
	}

	size_t pop()
	{
	    assert(nItems > 0);
	    size_t const tmp = item[head];

	    head = (head + 1) % size;
	    nItems -= 1;
	    return tmp;
	}
    };

    const uint16_t bank;

    T pool[size];
    CircBuf freeList;
    size_t maxActiveIdCount_;

 protected:
    std::bitset<size> inUse;

    IdPool(IdPool const&);
    IdPool& operator=(IdPool const&);

    T const* begin() const { return pool; }
    T* begin() { return pool; }
    T const* end() const { return pool + sizeof(pool) / sizeof(*pool); }
    T* end() { return pool + sizeof(pool) / sizeof(*pool); }

 public:
    IdPool() :
	bank(bankGen()), maxActiveIdCount_(0)
    {
#ifdef DEBUG
	assert(size > 0 && (((~size + 1) & size) ^ size) == 0);
#endif

	// Initially add all ids to the free list

	for (size_t ii = 0; ii < size; ii++)
	    freeList.push(ii);
    }

    // This function allows you to iterate through the active nodes of the pool. It's a linear search, so it's not as
    // efficient as iterating through a map. Since this is only being used by the reporting code, it's not as critical. To
    // start the iteration, pass a null pointer. The function will return the first active entry. From then on, pass the
    // value returned by the previous call to this function. Once the function returns null, you're done.

    T* next(T const* const entry) const
    {
#ifdef DEBUG
	if (entry) {
	    assert(entry >= begin() && entry < end());
	    assert(begin() + (entry - begin()) == entry);
	}
#endif
	for (size_t index = entry ? (entry - begin()) + 1 : 0; index < size; ++index)
	    if (inUse.test(index))
		return const_cast<T*>(begin() + index);
	return 0;
    }

    T* alloc()
    {
	if (freeList.total() > 0) {
	    size_t const idx = freeList.pop();

	    inUse.set(idx);
	    maxActiveIdCount_ = std::max(maxActiveIdCount_, activeIdCount());
	    return begin() + idx;
	}
	throw std::bad_alloc();
    }

    void release(T* const entry)
    {
	assert(entry);
	assert(entry >= begin() && entry < end());

	size_t const idx = entry - begin();

	assert(begin() + idx == entry);

	if (!inUse.test(idx))
	    syslog(LOG_WARNING, "RELEASE OF UNUSED ENTRY");

	freeList.push(idx);
	inUse.reset(idx);
    }

    bool beingUsed(T* const entry) const
    {
	assert(entry);
	assert(entry >= begin() && entry < end());

	size_t const idx = entry - begin();

	assert(begin() + idx == entry);

	return inUse.test(idx);
    }

    T* entry(R const id)
    {
	uint16_t ii = idToIndex(id);

	return (ii | bank) == id.raw() && ii < size && inUse.test(ii) ? begin() + ii : 0;
    }

    R id(T const* const entry) const
    {
	assert(entry);
	assert(entry >= begin() && entry < end());

	size_t const idx = entry - begin();

	assert(begin() + idx == entry);

	return R(idx | bank);
    }

    size_t freeIdCount() const
    {
	return freeList.total();
    }

    size_t activeIdCount() const
    {
	return size - freeList.total();
    }

    size_t maxActiveIdCount() const
    {
	return maxActiveIdCount_;
    }

 private:
    inline static uint16_t idToIndex(R const id)
    {
	return id.raw() & (size - 1);
    }

    static uint16_t bankGen()
    {
      return (uint16_t) ((random() & ~(size - 1)) | size);
    }
};

#endif

// Local Variables:
// mode:c++
// fill-column:125
// End:
