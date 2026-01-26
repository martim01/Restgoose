#ifndef PML_RESTGOOSE_THREADPOOL
#define PML_RESTGOOSE_THREADPOOL

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "dllexport.h"
#include "threadsafequeue.h"


namespace pml::restgoose
{
    class RG_EXPORT ThreadPool
    {
        public:
            /**
             * @brief Get the static ThreadPool singleton
             * 
             * @return ThreadPool& 
             */
            static ThreadPool& Get();

            /**
             * @brief Queue a function for the thread pool to run
             * 
             * @tparam FunctionType 
             * @param[in] f 
             */
            template<typename FunctionType> void Submit(FunctionType f)
            {
                m_work_queue.push(std::function<void()>(f));
            }

            /**
             * @brief Queue a function for the thread pool to run
             * 
             * @tparam Callable 
             * @tparam Args 
             * @param[in] func 
             * @param[in] args 
             */
            template<typename Callable, typename... Args> void Submit(Callable&& func, Args&&... args)
            {
                Submit([=]{func(args...);});
            }

            /**
             * @brief Create a worker threads
             * 
             * @param[in] nMinThreads the minimum number of threads there should be
             * @param[in] nMaxThreads the maximum number of threads there should be
             * @return size_t the number of threads there now are
             */
            size_t CreateWorkers(size_t nMinThreads, size_t nMaxThreads);

            /**
             * @brief Add worker threads
             * 
             * @param[in] nWorkers the number of threads to add
             * @return size_t the number of threads there now are
             */
            size_t AddWorkers(size_t nWorkers);

            /**
             * @brief Stop the threadpool
             * 
             */
            void Stop();

        private:
            ThreadPool();
            ~ThreadPool();

            void WorkerThread();

            std::atomic_bool m_bDone = ATOMIC_VAR_INIT(false);
            threadsafe_queue<std::function<void()>> m_work_queue;
            std::vector<std::thread> m_vThreads;
    };
}

#endif