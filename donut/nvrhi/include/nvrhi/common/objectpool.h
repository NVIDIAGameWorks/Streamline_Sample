#pragma once

namespace nvrhi {

// simple reference-counted object
class ReferenceCounter
{
public:
    ReferenceCounter() = default;
    // no copies of refcounted objects
    ReferenceCounter(const ReferenceCounter&) = delete;

    int32_t addref()
    {
        refcount++;
        return refcount;
    }

    int32_t release()
    {
        refcount--;
        assert(refcount >= 0);

        return refcount;
    }

    // For debugging.
    int32_t getrefcount() { return refcount; }

private:
    int32_t refcount = 0;
};

// an object pool for reusable API objects
// TContext is the renderer context object
// T is expected to derive from ReferenceCounter
// and implement the following methods:
//
// void reset(TContext& context);
// void destroy(TContext& context);
//
// reset() is called when an object is created or returned to the pool
// destroy() is called when the pool is destroyed
//
// setting DoNotAllocate = true inhibits allocating new objects
// this is useful when objects can't be fully initialized in the ctor

template <typename TContext, typename T, bool DoNotAllocate = false>
class ObjectPool
{
    TContext& context;

    nvrhi::list<T *> pool;
    uint32_t objectsAllocated = 0;

public:
    ObjectPool(TContext& context)
        : context(context)
    { }

    // non-copiable
    ObjectPool(const ObjectPool& other) = delete;

    T *get()
    {
        T *ret = nullptr;

        if (pool.size() == 0)
        {
            if (DoNotAllocate)
            {
                return nullptr;
            } else {
                ret = new (nvrhi::HeapAlloc) T();
                ret->reset(context);
                objectsAllocated++;
            }
        } else {
            ret = pool.back();
            pool.pop_back();
        }

        assert(ret);
        return ret;
    }

    void retire(T *obj)
    {
        assert(obj);

        obj->reset(context);
        pool.push_back(obj);
    }

    uint32_t numObjectsAllocated() { return objectsAllocated; }
    uint32_t numObjectsOutstanding() { return objectsAllocated - pool.size(); }
};

} // namespace nvrhi
