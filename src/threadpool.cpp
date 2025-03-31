#include "threadpool.h"

#include "log.h"

namespace pml::restgoose
{

ThreadPool& ThreadPool::Get()
{
    static ThreadPool pool;
    return pool;
}

ThreadPool::ThreadPool()
{
    AddWorkers(1);    //we want at least 1 threaad otherwise why are we calling this
}


size_t ThreadPool::CreateWorkers(size_t nMinThreads, size_t nMaxThreads)
{
    pmlLog(pml::LOG_TRACE, "pml::restgoose") << "ThreadPool - hardware concurreny = " << std::thread::hardware_concurrency();
    auto nThreads = std::max(nMinThreads, std::min(nMaxThreads, (size_t)std::thread::hardware_concurrency()));
    if(nThreads > m_vThreads.size())
    {
        return AddWorkers(nThreads-m_vThreads.size());
    }
    pmlLog(pml::LOG_TRACE, "pml::restgoose") << "Threadpool::CreateWorkers - now has " << m_vThreads.size() << " workers";
    return m_vThreads.size();
}

size_t ThreadPool::AddWorkers(size_t nWorkers)
{
   try
    {
        for(size_t i = 0; i < nWorkers; i++)
        {
            m_vThreads.push_back(std::thread(&ThreadPool::WorkerThread, this));
        }
        pmlLog(pml::LOG_TRACE, "pml::restgoose") << "Threadpool::AddWorkers - now has " << m_vThreads.size() << " workers";
        return m_vThreads.size();
    }
    catch(std::exception& e)
    {
        pmlLog(pml::LOG_WARN, "pml::restgoose") << "Threadpool::AddWorkers - failed to create all workers " << e.what() << " now has " << m_vThreads.size() << " workers";
        return m_vThreads.size();
    }
}

ThreadPool::~ThreadPool()
{
    Stop();
}

void ThreadPool::Stop()
{
    pmlLog(pml::LOG_TRACE, "pml::restgoose") << "ThreadPool - Stop";
    m_bDone = true;
    m_work_queue.exit();
    for(auto& th : m_vThreads)
    {
        th.join();
    }
    m_vThreads.clear();
    pmlLog(pml::LOG_TRACE, "pml::restgoose") << "ThreadPool - Stopped";
}

void ThreadPool::WorkerThread()
{
    while(!m_bDone)
    {
        std::function<void()> task;
        pmlLog(pml::LOG_TRACE, "pml::restgoose") << "ThreadPool - wait_and_pop";
        if(m_work_queue.wait_and_pop(task))
        {
            pmlLog(pml::LOG_TRACE, "pml::restgoose") << "ThreadPool - task()";
            task();
        }
    }
}

}