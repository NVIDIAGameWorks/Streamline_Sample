#if __linux__

#include <functional>
#include <donut/core/taskgroup.h>

// super-minimal (synchronous) implementation
concurrency::task_group::task_group() { }
void concurrency::task_group::run(std::function<void(void)> f) {f();}
void concurrency::task_group::wait() { /**/ }

#endif // __linux__
