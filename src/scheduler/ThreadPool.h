#pragma once

// ThreadPool.h — 固定大小工作线程池
//
// 职责：维护一组长期运行的工作线程，从 FIFO 队列中取出并执行工作项。
// 本类不涉及任务优先级，优先级排序由上层 Scheduler 在入队前完成。
//
// 同步模型：
//   - cv_     : 有新工作项时唤醒空闲工作线程
//   - idleCv_ : 队列为空且无活跃线程时唤醒 waitIdle() 的调用方
//   - activeCount_ : 记录当前正在执行工作项的线程数，配合 idleCv_ 实现精确的空闲检测

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace ThreadLoom {

// ThreadPool — 固定线程数的工作线程池
// 线程在构造时启动，在 stop() 或析构时退出
class ThreadPool {
public:
    using WorkItem = std::function<void()>;  // 工作项类型：无参无返回值的可调用对象

    // threadCount：工作线程数；传 0 时自动取 hardware_concurrency()，至少为 2
    explicit ThreadPool(size_t threadCount = 0);

    // 析构时自动调用 stop()，确保所有线程安全退出
    ~ThreadPool();

    // 不可拷贝、不可移动（持有线程和互斥量）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 向队列末尾追加一个工作项
    // 若线程池正在停止（stopping_ == true），则拒绝提交并返回 false
    bool submit(WorkItem work);

    // 阻塞调用线程，直到工作队列为空且所有工作线程均空闲
    // 可用于确认所有已提交工作项执行完毕
    void waitIdle();

    // 优雅停止：标记停止 → 广播唤醒所有工作线程 → join 等待线程退出
    // 已在队列中的工作项会被丢弃（工作线程检测到 stopping_ 后退出循环）
    void stop();

    // --- 只读统计 ---
    size_t threadCount()  const { return workers_.size(); }  // 工作线程总数
    size_t pendingCount() const;                              // 队列中待执行的工作项数
    bool   isStopped()   const { return stopping_.load(); }  // 是否已停止

private:
    // 每个工作线程运行的主循环：等待 → 取任务 → 执行 → 循环
    void workerLoop();

    std::vector<std::thread>  workers_;      // 工作线程列表
    std::queue<WorkItem>      queue_;        // FIFO 工作队列（受 mutex_ 保护）
    mutable std::mutex        mutex_;        // 保护 queue_ 和 activeCount_ 的互斥量
    std::condition_variable   cv_;           // 新任务入队时通知工作线程
    std::condition_variable   idleCv_;       // 线程池空闲时通知 waitIdle()
    std::atomic<bool>         stopping_{ false };   // 停止标志，提交和工作线程均会检查
    std::atomic<int>          activeCount_{ 0 };    // 当前正在执行工作项的线程数
};

} // namespace ThreadLoom
