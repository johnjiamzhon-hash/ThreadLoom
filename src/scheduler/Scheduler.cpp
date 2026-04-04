#include "Scheduler.h"
#include <cassert>

namespace ThreadLoom {

Scheduler::Scheduler(size_t threadCount)
    : pool_(threadCount)
    , dispatcher_([this] { dispatchLoop(); })  // 构造时立即启动调度线程
{}

Scheduler::~Scheduler() {
    stop();  // 确保析构时调度线程和线程池都已安全退出
}

Task::Id Scheduler::submit(std::unique_ptr<Task> task) {
    assert(task != nullptr);
    Task::Id id = task->id();  // 在移走 task 之前先取出 ID

    // 构造队列条目：记录优先级和提交序号，用于排序
    PriorityEntry entry{
        static_cast<int>(task->priority()),
        seqCounter_.fetch_add(1, std::memory_order_relaxed),  // 仅需唯一性，宽松序足够
        std::move(task)
    };

    {
        std::lock_guard<std::mutex> lock(pqMutex_);
        pq_.push(std::move(entry));
    }
    pqCv_.notify_one();  // 唤醒 dispatchLoop，有新任务可调度
    return id;
}

// 便捷重载：将 lambda 包装为 FunctionTask，转发给 submit(unique_ptr)
Task::Id Scheduler::submit(std::string name,
                            std::function<void()> fn,
                            TaskPriority priority) {
    return submit(std::make_unique<FunctionTask>(
        std::move(name), std::move(fn), priority));
}

void Scheduler::waitAll() {
    // 第一步：等待优先级队列排空（所有任务已被 dispatchLoop 取走并提交给线程池）
    {
        std::unique_lock<std::mutex> lock(pqMutex_);
        pqCv_.wait(lock, [this] { return pq_.empty(); });
    }
    // 第二步：等待线程池所有工作线程执行完毕（任务真正执行结束）
    pool_.waitIdle();
}

void Scheduler::stop() {
    // 先标记停止，再广播唤醒 dispatchLoop，使其能检测到退出条件
    running_.store(false, std::memory_order_release);
    pqCv_.notify_all();

    if (dispatcher_.joinable()) dispatcher_.join();  // 等待调度线程退出
    pool_.stop();                                     // 等待所有工作线程退出
}

size_t Scheduler::pendingCount() const {
    std::lock_guard<std::mutex> lock(pqMutex_);
    return pq_.size();
}

// 调度线程主循环：从优先级队列取任务，提交给线程池执行
void Scheduler::dispatchLoop() {
    while (true) {
        std::unique_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(pqMutex_);
            // 阻塞等待：直到有任务可调度，或收到停止信号
            pqCv_.wait(lock, [this] {
                return !running_.load() || !pq_.empty();
            });

            // 停止且队列为空：退出循环，调度线程正常结束
            if (!running_.load() && pq_.empty()) break;
            if (pq_.empty()) continue;  // 防御性检查（理论上不会触发）

            // 从堆顶取出最高优先级任务
            // pq_.top() 返回 const 引用，需要 const_cast 才能 move unique_ptr
            auto& top = const_cast<PriorityEntry&>(pq_.top());
            task = std::move(top.task);
            pq_.pop();

            // 队列刚刚变空，唤醒 waitAll() 中等待的线程
            if (pq_.empty()) pqCv_.notify_all();
        }

        // std::function 要求可拷贝的 callable，用 shared_ptr 包装 unique_ptr 以满足此要求
        auto sharedTask = std::shared_ptr<Task>(std::move(task));
        pool_.submit([sharedTask, this]() {
            sharedTask->execute();
            completedCount_.fetch_add(1, std::memory_order_relaxed);  // 执行完成，计数+1
        });
    }
}

} // namespace ThreadLoom
