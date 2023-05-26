#pragma once
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
//#include "response.h"
#include "log.h"
#include "threadsafequeue.h"
#include "dllexport.h"

namespace pml
{
    namespace restgoose
    {
        class RG_EXPORT ThreadPool
        {
            public:
                static ThreadPool& Get();

                template<typename FunctionType> void Submit(FunctionType f)
                {
                    m_work_queue.push(std::function<void()>(f));

                    //m_condition.notify_one();

                }
                template<typename Callable, typename... Args> void Submit(Callable&& func, Args&&... args)
                {
                    Submit([=]{func(args...);});
                }

                size_t CreateWorkers(size_t nMinThreads, size_t nMaxThreads);
                size_t AddWorkers(size_t nWorkers);

                void Stop();

            private:
                ThreadPool();
                ~ThreadPool();

                void WorkerThread();

                std::atomic_bool m_bDone;
                threadsafe_queue<std::function<void()>> m_work_queue;
                std::vector<std::thread> m_vThreads;

//                std::mutex m_mutex;
//                std::condition_variable m_condition;
        };
    };
};



