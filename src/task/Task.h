#pragma once

// Task.h — ThreadLoom 任务基类及相关枚举定义
// 所有可调度任务均继承自 Task，并实现 execute() 方法

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace ThreadLoom {

// 任务优先级：数值越大优先级越高，调度器优先执行高优先级任务
enum class TaskPriority : int {
    Low      = 0,  // 低优先级
    Normal   = 1,  // 普通优先级（默认）
    High     = 2,  // 高优先级
    Critical = 3   // 最高优先级，紧急任务使用
};

// 任务生命周期状态
enum class TaskState {
    Pending,    // 已提交，等待调度
    Running,    // 正在工作线程中执行
    Completed,  // 执行成功完成
    Failed,     // 执行过程中抛出异常
    Cancelled   // 已被取消，execute() 不会被调用（或提前退出）
};

// Task — 可调度任务的抽象基类
// 子类须实现 execute()，在其中执行实际工作逻辑
// Task 对象不可拷贝，以确保所有权语义清晰
class Task {
public:
    using Id = uint64_t;  // 全局唯一任务 ID 类型

    explicit Task(std::string name, TaskPriority priority = TaskPriority::Normal);
    virtual ~Task() = default;

    // 禁止拷贝；允许移动（调度器内部通过 unique_ptr 管理所有权）
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = default;
    Task& operator=(Task&&) = default;

    // 核心接口：子类必须实现，在工作线程中被调度器调用
    // 实现时应在开头检查 isCancelled()，以支持提前退出
    virtual void execute() = 0;

    // 请求取消任务
    // 仅对 Pending 状态的任务有效；Running 中的任务需在 execute() 内主动检测
    virtual void cancel();

    // 判断任务是否已被取消
    bool isCancelled() const { return state_ == TaskState::Cancelled; }

    // --- 只读访问器 ---
    Id                   id()         const { return id_; }
    const std::string&   name()       const { return name_; }
    TaskPriority         priority()   const { return priority_; }
    TaskState            state()      const { return state_.load(); }

    // 任务提交时刻（用于统计等待时间或超时检测）
    std::chrono::steady_clock::time_point submitTime() const { return submitTime_; }

    // 优先级比较：供优先级队列排序使用（值越大优先级越高）
    bool operator<(const Task& other) const {
        return static_cast<int>(priority_) < static_cast<int>(other.priority_);
    }

protected:
    // 供子类或调度器更新任务状态
    void setState(TaskState s) { state_.store(s); }

private:
    static std::atomic<Id> nextId_;  // 原子自增计数器，生成全局唯一 ID

    Id           id_;          // 本任务的唯一 ID
    std::string  name_;        // 任务名称，用于日志和调试
    TaskPriority priority_;    // 调度优先级
    std::atomic<TaskState> state_;              // 当前状态（原子，支持多线程安全读写）
    std::chrono::steady_clock::time_point submitTime_;  // 提交时间戳
};


// FunctionTask — 将任意可调用对象（lambda / 函数指针）包装为 Task
// 适合不需要自定义状态的简单任务，通过 Scheduler::submit() 的重载直接使用
class FunctionTask : public Task {
public:
    FunctionTask(std::string name,
                 std::function<void()> fn,
                 TaskPriority priority = TaskPriority::Normal)
        : Task(std::move(name), priority)
        , fn_(std::move(fn))
    {}

    // 执行包装的可调用对象；若任务已取消则跳过
    void execute() override {
        if (!isCancelled()) {
            fn_();
        }
    }

private:
    std::function<void()> fn_;  // 被包装的实际执行逻辑
};

} // namespace ThreadLoom
