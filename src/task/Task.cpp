#include "Task.h"

namespace ThreadLoom {

// 全局唯一 ID 计数器，从 1 开始，每次构造任务时原子自增
std::atomic<Task::Id> Task::nextId_{ 1 };

Task::Task(std::string name, TaskPriority priority)
    : id_(nextId_.fetch_add(1, std::memory_order_relaxed))  // 宽松序足够：仅要求唯一性，无需同步其他内存
    , name_(std::move(name))
    , priority_(priority)
    , state_(TaskState::Pending)
    , submitTime_(std::chrono::steady_clock::now())  // 记录提交时刻
{}

// 尝试将任务从 Pending 状态切换为 Cancelled
// 使用 CAS 保证线程安全：若任务已开始执行（非 Pending），则取消无效
void Task::cancel() {
    TaskState expected = TaskState::Pending;
    state_.compare_exchange_strong(expected, TaskState::Cancelled);
}

} // namespace ThreadLoom
