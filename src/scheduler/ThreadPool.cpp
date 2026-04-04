#include "ThreadPool.h"
#include <cassert>

namespace ThreadLoom {

ThreadPool::ThreadPool(size_t threadCount) {
    // 若未指定线程数，取 CPU 核心数；hardware_concurrency() 返回 0 时兜底用 2
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        if (threadCount == 0) threadCount = 2;
    }

    workers_.reserve(threadCount);
    for (size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });  // 每个线程运行 workerLoop
    }
}

ThreadPool::~ThreadPool() {
    stop();  // 确保析构时线程已全部退出，避免悬空线程
}

bool ThreadPool::submit(WorkItem work) {
    // 用 acquire 语序读取 stopping_，确保看到 stop() 写入的最新值
    if (stopping_.load(std::memory_order_acquire)) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(work));
    }
    cv_.notify_one();  // 唤醒一个空闲工作线程来处理新任务
    return true;
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    // 同时满足两个条件才算真正空闲：
    //   1. 队列为空（没有待执行的工作项）
    //   2. activeCount_ == 0（没有正在执行的工作线程）
    idleCv_.wait(lock, [this] {
        return queue_.empty() && activeCount_.load() == 0;
    });
}

void ThreadPool::stop() {
    // 先设置停止标志，再广播唤醒所有阻塞在 cv_.wait() 的工作线程
    stopping_.store(true, std::memory_order_release);
    cv_.notify_all();

    // 逐一 join，确保所有工作线程完全退出后再清空列表
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    workers_.clear();
}

size_t ThreadPool::pendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

// 工作线程主循环：持续从队列取工作项并执行，直到收到停止信号且队列为空
void ThreadPool::workerLoop() {
    while (true) {
        WorkItem work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // 阻塞等待：有任务可取，或收到停止信号
            cv_.wait(lock, [this] {
                return stopping_.load() || !queue_.empty();
            });

            // 停止且队列已清空：退出循环，工作线程结束
            if (stopping_.load() && queue_.empty()) break;

            work = std::move(queue_.front());
            queue_.pop();
            // 在锁内递增 activeCount_，确保 waitIdle() 不会在任务取出后、执行前误判为空闲
            activeCount_.fetch_add(1, std::memory_order_relaxed);
        }

        work();  // 在锁外执行工作项，避免长时间持锁阻塞其他线程

        {
            // 在锁内递减 activeCount_，保证与 waitIdle() 的条件检查互斥
            std::lock_guard<std::mutex> lock(mutex_);
            activeCount_.fetch_sub(1, std::memory_order_relaxed);
        }
        // 通知所有等待 waitIdle() 的线程重新检查空闲条件
        idleCv_.notify_all();
    }
}

} // namespace ThreadLoom
