#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <stop_token>

template<typename T>
class CCommandQueue
{
public:
    CCommandQueue() = default;
    ~CCommandQueue() = default;

    CCommandQueue(const CCommandQueue&) = delete;
    CCommandQueue& operator=(const CCommandQueue&) = delete;
    CCommandQueue(CCommandQueue&&) = delete;
    CCommandQueue& operator=(CCommandQueue&&) = delete;

public:
    void Push(T command)
    {
        {
            std::lock_guard lock(m_mutex);
            m_queue.push(std::move(command));
        }

        m_cv.notify_one();
    }

    bool Pop(T& command, std::stop_token stopToken)
    {
        std::unique_lock lock(m_mutex);

        const bool ready = m_cv.wait(lock, stopToken, [this] { return !m_queue.empty(); });

        if (!ready || stopToken.stop_requested())
            return false;

        command = std::move(m_queue.front());
        m_queue.pop();

        return true;
    }

    bool WaitPopFor(T& command, std::chrono::milliseconds timeout, std::stop_token stopToken)
    {
        std::unique_lock lock(m_mutex);

        const bool ready = m_cv.wait_for(lock, stopToken, timeout, [this] { return !m_queue.empty(); });

        if (!ready || stopToken.stop_requested())
            return false;

        command = std::move(m_queue.front());
        m_queue.pop();

        return true;
    }

    bool TryPop(T& command)
    {
        std::lock_guard lock(m_mutex);

        if (m_queue.empty())
            return false;

        command = std::move(m_queue.front());
        m_queue.pop();

        return true;
    }

    void Clear()
    {
        std::lock_guard lock(m_mutex);
        std::queue<T> empty;
        m_queue.swap(empty);
    }

    void NotifyAll()
    {
        m_cv.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable_any m_cv;
};
