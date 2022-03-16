#pragma once

#ifndef _WIN32

// declare internal implementation of concurrency::task_group

#include <functional>

namespace concurrency
{
	// minimal serialized implementation of concurrency::task_group
	template<class _Function>
	class task_handle;
	
	class task_group
	{
		public:
		task_group();
		void run(std::function<void(void)> f);
		void wait();
	};
}

#endif // _WIN32
