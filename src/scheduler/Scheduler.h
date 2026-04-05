#pragma once

// Scheduler.h — 基于优先级的任务调度器

//
// 架构概述：
//   Scheduler 内部维护一个优先级队列（pq_）和一个专属调度线程（dispatcher_）。
//   调度线程持续从队列中取出最高优先级任务，提交给 ThreadPool 执行。
//   同优先级任务按提交顺序（FIFO）执行，由 seqCounter_ 保证。
//
// 线程模型：
//   - 调用方线程：调用 submit() 将任务压入优先级队列
//   - dispatcher_ 线程：唯一消费者，从队列取任务并转发给线程池
//   - ThreadPool 工作线程：实际执行 task->execute()

#include "ThreadPool.h"
#include "../task/Task.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ThreadLoom
{

    // Scheduler — 优先级任务调度器，底层由线程池驱动
    //
    // 调度规则：Critical > High > Normal > Low；同优先级按提交顺序（FIFO）执行
    class Scheduler
    {
    public:
        // threadCount：工作线程数；传 0 时由 ThreadPool 自动取 hardware_concurrency()
        explicit Scheduler(size_t threadCount = 0);

        // 析构时自动调用 stop()，等待调度线程和工作线程退出
        ~Scheduler();

        // 不可拷贝、不可移动（持有线程和互斥量，语义上唯一）
        Scheduler(const Scheduler &) = delete;
        Scheduler &operator=(const Scheduler &) = delete;

        // 提交自定义任务（转移所有权），返回全局唯一任务 ID
        Task::Id submit(std::unique_ptr<Task> task);

        // 便捷重载：将可调用对象包装为 FunctionTask 后提交
        Task::Id submit(std::string name,
                        std::function<void()> fn,
                        TaskPriority priority = TaskPriority::Normal);

        // 阻塞调用线程，直到优先级队列为空且线程池所有工作线程空闲
        // 注意：waitAll() 期间仍可继续 submit()，新任务会被等待
        void waitAll();

        // 停止接受新任务，排空队列后终止调度线程和线程池
        // 析构函数会自动调用，通常不需要手动调用
        void stop();

        // 调度器是否处于运行状态（stop() 调用后返回 false）
        bool isRunning() const { return running_.load(); }

        // --- 统计接口 ---
        size_t pendingCount() const;                                     // 队列中待调度的任务数
        size_t completedCount() const { return completedCount_.load(); } // 已执行完成的任务总数

    private:
        // 简介：优先级队列的元素：将任务与其调度元数据打包存储
        // 【PriorityEntry 结构】
        //   作用：将任务与其调度元数据（优先级 + 提交序号）打包存储
        //   字段说明：
        //     - priority: 优先级数值，遵循 Critical > High > Normal > Low
        //     - seqNum: 全局递增的提交序号，用于同优先级任务的 FIFO 排序
        //     - task: 指向实际任务对象的唯一指针
        struct PriorityEntry
        {
            int priority;    // 优先级数值，越大越优先
            uint64_t seqNum; // 提交序号，同优先级时数值越小越先执行（FIFO）
            std::unique_ptr<Task> task;

            // greater<> 比较器使 std::priority_queue 变为最大堆
            // greater(a,b) 即 a>b，返回true说明a大，a浮顶，
            // 如果seqNum的比较方法和greater相反，则在比较seqNum时选择相反的返回值
            bool operator>(const PriorityEntry &o) const
            {
                if (priority != o.priority)
                    return priority > o.priority;
                return seqNum < o.seqNum; // seqNum 小的排在前面
            }
        };

        // 调度线程主循环：等待队列非空 → 取出最高优先级任务 → 提交给线程池
        void dispatchLoop();

        ThreadPool pool_; // 底层工作线程池

        // 优先级队列：使用 greater<> 实现最大堆（priority 高、seqNum 小的元素优先弹出）
        // 【std::priority_queue + std::greater 协作原理】：
        //   - PQ 定义使用 std::greater<PriorityEntry> 作为比较器
        //   - std::greater 会调用 operator> 来判断：第一个元素 > 第二个元素 是否成立
        //   - 当 operator>(a, b) 返回 true 时，a 被视为"更优先"，在堆中位置更靠前
        //   - std::priority_queue 默认是最大堆，std::greater 反向此行为，使其成为最小堆
        //   - 结果：每次 pop() 得到的是同时满足"最高优先级"和"最早提交"的任务
        using PQ = std::priority_queue<PriorityEntry,
                                       std::vector<PriorityEntry>,
                                       std::greater<PriorityEntry>>;
        PQ pq_;                        // 待调度任务队列
        mutable std::mutex pqMutex_;   // 保护 pq_ 的互斥量（submit/dispatchLoop/waitAll 共用）
        std::condition_variable pqCv_; // 用于唤醒 dispatchLoop 和 waitAll

        std::thread dispatcher_;                // 专属调度线程，运行 dispatchLoop()
        std::atomic<bool> running_{true};       // false 表示已请求停止
        std::atomic<uint64_t> seqCounter_{0};   // 全局提交序号，用于 FIFO 排序
        std::atomic<size_t> completedCount_{0}; // 已完成任务计数
    };

} // namespace ThreadLoom
