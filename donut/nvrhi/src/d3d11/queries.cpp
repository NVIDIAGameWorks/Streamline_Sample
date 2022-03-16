#include <nvrhi/d3d11/d3d11.h>
#include <nvrhi/common/containers.h>

namespace nvrhi 
{
namespace d3d11
{

#ifdef _DEBUG
#define CHECK_ERROR(expr, msg) if (!(expr)) this->message(MessageSeverity::Error, msg, __FILE__, __LINE__)
#else
#define CHECK_ERROR(expr, msg) if (!(expr)) this->message(MessageSeverity::Error, msg)
#endif

EventQueryHandle Device::createEventQuery(void)
{
    EventQuery *ret = new EventQuery(this);

    D3D11_QUERY_DESC queryDesc;
    queryDesc.Query = D3D11_QUERY_EVENT;
    queryDesc.MiscFlags = 0;

    HRESULT hr;
    hr = device->CreateQuery(&queryDesc, &ret->query);
    CHECK_ERROR(SUCCEEDED(hr), "failed to create query");

    if (!SUCCEEDED(hr))
    {
        delete ret;
        return nullptr;
    }

    return EventQueryHandle::Create(ret);
}

void Device::setEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    context->End(query->query.Get());
}

bool Device::pollEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    if (query->resolved)
    {
        return true;
    }

    HRESULT hr;
    hr = context->GetData(query->query.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);

    if (SUCCEEDED(hr))
    {
        query->resolved = true;
        return true;
    } else {
        return false;
    }
}

void Device::waitEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    if (query->resolved)
    {
        return;
    }

    HRESULT hr;

    do {
        hr = context->GetData(query->query.Get(), nullptr, 0, 0);
    } while (hr == S_FALSE);

    assert(SUCCEEDED(hr));
}

void Device::resetEventQuery(IEventQuery* _query)
{
    EventQuery* query = static_cast<EventQuery*>(_query);

    query->resolved = false;
}

TimerQueryHandle Device::createTimerQuery(void)
{
    TimerQuery *ret = new TimerQuery(this);

    D3D11_QUERY_DESC queryDesc;
    HRESULT hr;

    queryDesc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    queryDesc.MiscFlags = 0;

    hr = device->CreateQuery(&queryDesc, &ret->disjoint);
    CHECK_ERROR(SUCCEEDED(hr), "failed to create query");

    if (!SUCCEEDED(hr))
    {
        delete ret;
        return nullptr;
    }

    queryDesc.Query = D3D11_QUERY_TIMESTAMP;
    queryDesc.MiscFlags = 0;

    hr = device->CreateQuery(&queryDesc, &ret->start);
    CHECK_ERROR(SUCCEEDED(hr), "failed to create query");

    if (!SUCCEEDED(hr))
    {
        delete ret;
        return nullptr;
    }

    hr = device->CreateQuery(&queryDesc, &ret->end);
    CHECK_ERROR(SUCCEEDED(hr), "failed to create query");

    if (!SUCCEEDED(hr))
    {
        delete ret;
        return nullptr;
    }

    return TimerQueryHandle::Create(ret);
}

void Device::beginTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    assert(!query->resolved);
    context->Begin(query->disjoint.Get());
    context->End(query->start.Get());
}

void Device::endTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    assert(!query->resolved);
    context->End(query->end.Get());
    context->End(query->disjoint.Get());
}

bool Device::pollTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    if (query->resolved)
    {
        return true;
    }

    HRESULT hr;
    hr = context->GetData(query->disjoint.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);

    if (SUCCEEDED(hr))
    {
        // note: we don't mark this as resolved since we need to read data back and compute timing info
        // this is done in getTimerQueryTimeMS
        return true;
    } else {
        return false;
    }
}

float Device::getTimerQueryTime(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    if (!query->resolved)
    {
        HRESULT hr;

        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;

        do {
            hr = context->GetData(query->disjoint.Get(), &disjointData, sizeof(disjointData), 0);
        } while (hr == S_FALSE);
        assert(SUCCEEDED(hr));

        query->resolved = true;

        if (disjointData.Disjoint == TRUE)
        {
            // query resolved but captured invalid timing data
            query->time = 0.f;
        } else {
            UINT64 startTime = 0, endTime = 0;
            do {
                hr = context->GetData(query->start.Get(), &startTime, sizeof(startTime), 0);
            } while (hr == S_FALSE);
            assert(SUCCEEDED(hr));

            do {
                hr = context->GetData(query->end.Get(), &endTime, sizeof(endTime), 0);
            } while (hr == S_FALSE);
            assert(SUCCEEDED(hr));

            double delta = double(endTime - startTime);
            double frequency = double(disjointData.Frequency);
            query->time = float(delta / frequency);
        }
    }

    return query->time;
}

void Device::resetTimerQuery(ITimerQuery* _query)
{
    TimerQuery* query = static_cast<TimerQuery*>(_query);

    query->resolved = false;
    query->time = 0.f;
}

void Device::beginMarker(const char* name)
{
    if (userDefinedAnnotation)
    {
        wchar_t bufW[1024];
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, name, -1, bufW, ARRAYSIZE(bufW));

        userDefinedAnnotation->BeginEvent(bufW);
    }
}

void Device::endMarker()
{
    if (userDefinedAnnotation)
    {
        userDefinedAnnotation->EndEvent();
    }
}

} // namespace d3d11
} // namespace nvrhi