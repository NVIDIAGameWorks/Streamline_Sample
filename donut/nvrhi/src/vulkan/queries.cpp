#include <nvrhi/vulkan.h>

namespace nvrhi
{
namespace vulkan
{

EventQueryHandle Device::createEventQuery(void)
{
    EventQuery *query = new (nvrhi::HeapAlloc) EventQuery(this);
    return EventQueryHandle::Create(query);
}

unsigned long EventQuery::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyEventQuery(this); 
    return result; 
}

void Device::destroyEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    if (query->started && !query->fence)
    {
        assert(!query->resolved);

        // fence has not been written yet
        // flush the command list to force the fence to be written back
        flushCommandList();
    }

    if (query->fence)
    {
        assert(query->started);
        query->fence->release();
    }

    heapDelete(query);
}

void Device::setEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    // ensure a command buffer exists to be kicked off
    // this is so we know which queue to add the query to
    auto *cmd = getAnyCmdBuf();
    assert(cmd);

    assert(query->fence == nullptr);
    assert(query->started == false);
    assert(query->resolved == false);

    queues[cmd->targetQueueID].addSubmitFenceListener(&query->fence);
    query->started = true;
}

bool Device::pollEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    assert(query->started == true);

    if (!query->resolved)
    {
        if (!query->fence)
        {
            return false;
        }

        if (query->fence->check(context))
        {
            query->resolved = true;
        }
    }

    return query->resolved;
}

void Device::waitEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    assert(query->started == true);

    if (!query->resolved)
    {
        if (!query->fence)
        {
            flushCommandList();
        }

        assert(query->fence);

        while (!pollEventQuery(query))
            ;
    }

    assert(query->resolved);
}

void Device::resetEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    if (query->fence)
    {
        query->fence->release();
    }

    query->fence = nullptr;
    query->started = false;
    query->resolved = false;
}


TimerQueryHandle Device::createTimerQuery(void)
{
    vk::Result res;

    if (!timerQueryPool)
    {
        // set up the timer query pool on first use
        auto poolInfo = vk::QueryPoolCreateInfo()
                            .setQueryType(vk::QueryType::eTimestamp)
                            .setQueryCount(numTimerQueries);

        res = context.device.createQueryPool(&poolInfo, context.allocationCallbacks, &timerQueryPool);
        CHECK_VK_FAIL(res);
        nameVKObject(timerQueryPool);
    }

    // reuse an existing query
    TimerQuery *query = timerQueryObjectPool.get();
    if (!query)
    {
        // no objects available
        // allocate a slot in the query pool
        uint32_t beginQueryIndex = nextTimerQueryIndex++;
        uint32_t endQueryIndex = nextTimerQueryIndex++;
        if (beginQueryIndex >= numTimerQueries ||
            endQueryIndex >= numTimerQueries)
        {
            // no more slots! raise Device::numTimerQueries
            return nullptr;
        }

        query = new TimerQuery(this);
        query->reset(context);
        query->beginQueryIndex = beginQueryIndex;
        query->endQueryIndex = endQueryIndex;
    }

    // in case the pool has created a query using its default ctor
    query->parent = this;

    return TimerQueryHandle::Create(query);
}

unsigned long TimerQuery::Release() 
{
    assert(refCount > 0);
    unsigned long result = --refCount; 
    if (result == 0) parent->destroyTimerQuery(this); 
    return result; 
}

void Device::destroyTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    timerQueryObjectPool.retire(query);
}

void Device::beginTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    assert(!query->started);
    assert(!query->resolved);

    auto *cmd = getAnyCmdBuf();
    assert(cmd);
    cmd->cmdBuf.resetQueryPool(timerQueryPool, query->beginQueryIndex, 2);
    cmd->cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timerQueryPool, query->beginQueryIndex);
}

void Device::endTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    assert(!query->started);
    assert(!query->resolved);

    auto *cmd = getAnyCmdBuf();
    assert(cmd);
    cmd->cmdBuf.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timerQueryPool, query->endQueryIndex);
    query->started = true;
}

bool Device::pollTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    assert(query->started);

    if (query->resolved)
    {
        return true;
    }

    uint32_t timestamps[2] = { 0, 0 };

    vk::Result res;
    res = context.device.getQueryPoolResults(timerQueryPool,
                                             query->beginQueryIndex, 2,
                                             sizeof(timestamps), timestamps,
                                             sizeof(timestamps[0]), vk::QueryResultFlags());
    assert(res == vk::Result::eSuccess || res == vk::Result::eNotReady);

    if (res == vk::Result::eNotReady)
    {
        return false;
    }

    const auto timestampPeriod = context.physicalDeviceProperties.limits.timestampPeriod;
    const float scale = 1e6f * timestampPeriod;

    query->time = float(timestamps[1] - timestamps[0]) / scale;
    query->resolved = true;
    return true;
}

float Device::getTimerQueryTime(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    assert(query->started);

    if (!query->resolved)
    {
        if (!pollTimerQuery(query))
        {
            flushCommandList();
            while(!pollTimerQuery(query))
                ;
        }
    }

    assert(query->resolved);
    return query->time;
}

void Device::resetTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    query->reset(context);
}


void Device::beginMarker(const char* name)
{
    if (context.extensions.EXT_debug_marker)
    {
        auto *cmd = getAnyCmdBuf();
        assert(cmd);

        auto markerInfo = vk::DebugMarkerMarkerInfoEXT()
                            .setPMarkerName(name);
        cmd->cmdBuf.debugMarkerBeginEXT(&markerInfo);
    }
}

void Device::endMarker()
{
    if (context.extensions.EXT_debug_marker)
    {
        auto *cmd = getAnyCmdBuf();
        assert(cmd);
        cmd->cmdBuf.debugMarkerEndEXT();
    }
}

} // namespace vulkan
} // namespace nvrhi