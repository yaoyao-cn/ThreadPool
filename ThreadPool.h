#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <type_traits>

class ThreadPool
{
	ThreadPool(const ThreadPool&) = delete;
public:
    ThreadPool(size_t threads);
    template<class F, class... Args>
    decltype(auto)  enqueue(F&& f, Args&&... args);
    ~ThreadPool();

private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > m_workers;
    // the task queue
    std::queue< std::function<void()> > m_tasks;
    
    // synchronization
    std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    bool m_stop = false;
};
 
// the constructor just launches some amount of m_workers
inline ThreadPool::ThreadPool(size_t threads)
{
    for(size_t i = 0;i<threads;++i)
        m_workers.emplace_back(
            [this]
            {
                for(;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->m_queue_mutex);
                        this->m_condition.wait(lock,
                            [this]{ return this->m_stop || !this->m_tasks.empty(); });
                        if(this->m_stop && this->m_tasks.empty())
                            return;
                        task = std::move(this->m_tasks.front());
                        this->m_tasks.pop();
                    }

                    task();
                }
            }
        );
}

// add new work item to the pool
template<class F, class... Args>
decltype(auto) ThreadPool::enqueue(F&& f, Args&&... args)
{
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
    auto res = task->get_future();
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(m_stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        m_tasks.emplace([task](){ (*task)(); });
    }
    m_condition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_stop = true;
    }
    m_condition.notify_all();

    for(auto& worker: m_workers)
    {
        worker.join();
    }
}

#endif
