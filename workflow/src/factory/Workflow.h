/*
  Copyright (c) 2019 Sogou, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Author: Xie Han (xiehan@sogou-inc.com)
*/

#ifndef _WORKFLOW_H_
#define _WORKFLOW_H_

#include <assert.h>
#include <stddef.h>
#include <utility>
#include <functional>
#include <mutex>
#include "SubTask.h"

class SeriesWork;
class ParallelWork;

using series_callback_t = std::function<void (const SeriesWork *)>;
using parallel_callback_t = std::function<void (const ParallelWork *)>;

class Workflow
{
public:
	static SeriesWork *
	create_series_work(SubTask *first, series_callback_t callback);

	static void
	start_series_work(SubTask *first, series_callback_t callback);

	static ParallelWork *
	create_parallel_work(parallel_callback_t callback);

	static ParallelWork *
	create_parallel_work(SeriesWork *const all_series[], size_t n,
						 parallel_callback_t callback);

	static void
	start_parallel_work(SeriesWork *const all_series[], size_t n,
						parallel_callback_t callback);

public:
	static SeriesWork *
	create_series_work(SubTask *first, SubTask *last,
					   series_callback_t callback);

	static void
	start_series_work(SubTask *first, SubTask *last,
					  series_callback_t callback);
};

class SeriesWork
{
public:
	void start()  // 启动串行
	{
		assert(!this->in_parallel);  
		this->first->dispatch();
	}

	/* Call dismiss() only when you don't want to start a created series.
	 * This operation is recursive, so only call on the "root". */
	void dismiss()   // 放弃串行中的所有任务
	{
		assert(!this->in_parallel);
		this->dismiss_recursive();
	}

public:
	void push_back(SubTask *task);   // 在串行尾部添加任务
	void push_front(SubTask *task);  // 在串行头部添加任务

public:
	void *get_context() const { return this->context; }
	void set_context(void *context) { this->context = context; }

public:
	/* Cancel a running series. Typically, called in the callback of a task
	 * that belongs to the series. All subsequent tasks in the series will be
	 * destroyed immediately and recursively (ParallelWork), without callback.
	 * But the callback of this canceled series will still be called. */
	void cancel() { this->canceled = true; }    // 取消串行

	/* Parallel work's callback may check the cancellation state of each
	 * sub-series, and cancel it's super-series recursively. */ 
	bool is_canceled() const { return this->canceled; }   // 串行是否被取消

public:
	// 为串行设置回调函数，将参数设置为nullptr可以取消回调
	void set_callback(series_callback_t callback)
	{
		this->callback = std::move(callback);
	}

public:
	/* The next 3 methods are intended for task implementations only. */
	SubTask *pop();

	void set_last_task(SubTask *last)
	{
		last->set_pointer(this);
		this->last = last;
	}

	void unset_last_task() { this->last = NULL; }

protected:
	void *context;
	series_callback_t callback;

private:
	SubTask *pop_task();
	void expand_queue();
	void dismiss_recursive();

private:
	SubTask *first;  // 第一个task
	SubTask *last;   // 最后一个task
	SubTask **queue;  
	int queue_size;
	int front;
	int back;
	bool in_parallel;
	bool canceled;
	std::mutex mutex;

protected:
	SeriesWork(SubTask *first, series_callback_t&& callback);
	virtual ~SeriesWork() { delete []this->queue; }
	friend class ParallelWork;
	friend class Workflow;
};

// 获取task所在的series
static inline SeriesWork *series_of(const SubTask *task)
{
	return (SeriesWork *)task->get_pointer();
}

// *series << task;  == series->push_back(task);
static inline SeriesWork& operator *(const SubTask& task)
{
	return *series_of(&task);
}

static inline SeriesWork& operator << (SeriesWork& series, SubTask *task)
{
	series.push_back(task);
	return series;
}

inline SeriesWork *
Workflow::create_series_work(SubTask *first, series_callback_t callback)
{
	return new SeriesWork(first, std::move(callback));
}

inline void
Workflow::start_series_work(SubTask *first, series_callback_t callback)
{
	new SeriesWork(first, std::move(callback));
	first->dispatch();
}

inline SeriesWork *
Workflow::create_series_work(SubTask *first, SubTask *last,
							 series_callback_t callback)
{
	SeriesWork *series = new SeriesWork(first, std::move(callback));
	series->set_last_task(last);
	return series;
}

inline void
Workflow::start_series_work(SubTask *first, SubTask *last,
							series_callback_t callback)
{
	SeriesWork *series = new SeriesWork(first, std::move(callback));
	series->set_last_task(last);
	first->dispatch();
}

class ParallelWork : public ParallelTask
{
public:
	// 并行是一种任务，启动并行会将其放入串行，并启动串行
	void start()
	{
		assert(!series_of(this));
		Workflow::start_series_work(this, nullptr);
	}

	// 取消并行中的所有任务
	void dismiss()
	{
		assert(!series_of(this));
		this->dismiss_recursive();
	}

public:
	void add_series(SeriesWork *series);

public:
	void *get_context() const { return this->context; }
	void set_context(void *context) { this->context = context; }

public:
	const SeriesWork *series_at(size_t index) const
	{
		if (index < this->subtasks_nr)
			return this->all_series[index];
		else
			return NULL;
	}

	const SeriesWork& operator[] (size_t index) const
	{
		return *this->series_at(index);
	}

	// 获取并行中串行的个数
	size_t size() const { return this->subtasks_nr; }

public:
	void set_callback(parallel_callback_t callback)
	{
		this->callback = std::move(callback);
	}

protected:
	virtual SubTask *done();

protected:
	void *context;
	parallel_callback_t callback;

private:
	void expand_buf();
	void dismiss_recursive();

private:
	size_t buf_size;
	SeriesWork **all_series;

protected:
	ParallelWork(parallel_callback_t&& callback);
	ParallelWork(SeriesWork *const all_series[], size_t n,
				 parallel_callback_t&& callback);
	virtual ~ParallelWork() { delete []this->subtasks; }
	friend class SeriesWork;
	friend class Workflow;
};

inline ParallelWork *
Workflow::create_parallel_work(parallel_callback_t callback)
{
	return new ParallelWork(std::move(callback));
}

inline ParallelWork *
Workflow::create_parallel_work(SeriesWork *const all_series[], size_t n,
							   parallel_callback_t callback)
{
	return new ParallelWork(all_series, n, std::move(callback));
}

inline void
Workflow::start_parallel_work(SeriesWork *const all_series[], size_t n,
							  parallel_callback_t callback)
{
	ParallelWork *p = new ParallelWork(all_series, n, std::move(callback));
	Workflow::start_series_work(p, nullptr);
}

#endif

