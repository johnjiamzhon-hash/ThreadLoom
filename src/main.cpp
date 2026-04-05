// ThreadLoom 调度器演示程序
// 展示如何使用 Scheduler 提交不同优先级的任务（函数任务和自定义任务）

#include "scheduler/Scheduler.h"
#include "task/Task.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace ThreadLoom;

// 自定义计算任务：继承 Task，在 execute() 中完成具体逻辑
// 构造时接收任务名、输入值和优先级
class ComputeTask : public Task
{
public:
    ComputeTask(std::string name, int value, TaskPriority prio)
        : Task(std::move(name), prio), value_(value)
    {
    }

    // 执行任务：模拟耗时计算（平方运算），并打印结果
    void execute() override
    {
        if (isCancelled())
            return; // 若任务已被取消则提前退出

        // 模拟 50ms 的计算耗时
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        int result = value_ * value_;

        std::printf("[Task %llu] %s  =>  %d^2 = %d\n",
                    static_cast<unsigned long long>(id()),
                    name().c_str(),
                    value_,
                    result);
    }

private:
    int value_; // 参与计算的输入值
};

int main()
{
    std::printf("=== ThreadLoom Scheduler Demo ===\n\n");

    // 创建调度器，启动 4 个工作线程
    Scheduler scheduler(9);

    // --- 提交不同优先级的 lambda 函数任务 ---
    // 调度器会按优先级顺序（Critical > High > Normal > Low）调度执行
    scheduler.submit("low-1", []
                     { std::printf("[FnTask] Low priority #1\n"); }, TaskPriority::Low);
    scheduler.submit("low-2", []
                     { std::printf("[FnTask] Low priority #2\n"); }, TaskPriority::Low);
    scheduler.submit("normal-1", []
                     { std::printf("[FnTask] Normal priority #1\n"); }, TaskPriority::Normal);
    scheduler.submit("normal-2", []
                     { std::printf("[FnTask] Normal priority #2\n"); }, TaskPriority::Normal);
    scheduler.submit("high-1", []
                     { std::printf("[FnTask] High priority #1\n"); }, TaskPriority::High);
    scheduler.submit("critical-1", []
                     { std::printf("[FnTask] Critical priority #1\n"); }, TaskPriority::Critical);

    // 提交自定义 ComputeTask 任务（通过 unique_ptr 转移所有权）
    scheduler.submit(std::make_unique<ComputeTask>("compute-A", 7, TaskPriority::High));
    scheduler.submit(std::make_unique<ComputeTask>("compute-B", 13, TaskPriority::Normal));
    scheduler.submit(std::make_unique<ComputeTask>("compute-C", 42, TaskPriority::Low));

    std::printf("Submitted 9 tasks. Waiting for completion...\n\n");

    // 启动调度器开始处理任务
    scheduler.start();

    // 阻塞主线程，直到所有已提交任务执行完毕
    scheduler.waitAll();

    std::printf("\n=== All tasks completed. Total: %zu ===\n",
                scheduler.completedCount());

    std::printf("\nPress Enter to exit...\n");
    std::getchar();

    return 0;
}
