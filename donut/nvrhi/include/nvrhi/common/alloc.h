#pragma once

#include <cstddef>

namespace nvrhi {

// define an allocator struct
struct IHeapAllocator
{
    virtual void *allocate(size_t len) = 0;
    virtual void release(void *ptr) = 0;
};

// sets the global allocator for NVRHI
void setAllocator(IHeapAllocator *allocator);

// NVRHI-internal memory allocation functions are below

IHeapAllocator *getDefaultAllocator(void);
void *heapAllocate(size_t len);
void heapFree(void *ptr);

template <class T>
void heapDelete(T *obj)
{
    obj->~T();
    heapFree(obj);
}

// std::allocator-compatible allocator using the global IHeapAllocator
template <class T>
struct HeapContainerAllocator
{
    typedef T value_type;

    HeapContainerAllocator() = default;

    template <class U>
    HeapContainerAllocator(const HeapContainerAllocator<U>&)
    { }

    T* allocate(std::size_t n)
    {
        return reinterpret_cast<T*>(heapAllocate(n * sizeof(T)));
    }

    void deallocate(T *p, std::size_t n)
    {
        (void)n;
        heapFree(p);
    }
};

template <class T, class U>
static constexpr bool operator==(const HeapContainerAllocator<T>&, const HeapContainerAllocator<U>&)
{
    return true;
}

template <class T, class U>
static constexpr bool operator!=(const HeapContainerAllocator<T>&, const HeapContainerAllocator<U>&)
{
    return false;
}

// allocator tag type for operator new to use our internal allocator
// e.g., Object *obj = new (nvrhi::HeapAlloc) Object(); ... heapDelete(obj);
struct HeapAllocTag_T { };
static const HeapAllocTag_T HeapAlloc;

} // namespace nvrhi

// new/delete adapters for IHeapAllocator
extern void * operator new (std::size_t size, nvrhi::HeapAllocTag_T);
extern void operator delete (void *p, nvrhi::HeapAllocTag_T);
