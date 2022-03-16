#include <nvrhi/common/alloc.h>

#include <malloc.h>

namespace nvrhi 
{

static struct DefaultHeapAllocator : public IHeapAllocator
{
    void *allocate(size_t len) override
    {
        return malloc(len);
    }

    void release(void *ptr) override
    {
        free(ptr);
    }
} defaultHeapAllocator;

// global NVRHI allocator (note: not per-thread)
static IHeapAllocator *global_allocator = &defaultHeapAllocator;

void setAllocator(IHeapAllocator *allocator)
{
    global_allocator = allocator;
}

void *heapAllocate(size_t len)
{
    return global_allocator->allocate(len);
}

void heapFree(void *ptr)
{
    global_allocator->release(ptr);
}

} // namespace nvrhi

void * operator new (std::size_t len, nvrhi::HeapAllocTag_T)
{
    return nvrhi::heapAllocate(len);
}

void operator delete (void *ptr, nvrhi::HeapAllocTag_T)
{
    return nvrhi::heapFree(ptr);
}
