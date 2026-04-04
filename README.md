# ThreadLoom

<div align="center">

**ThreadLoom** 是一个高性能的 C++17 多线程任务调度系统，支持优先级队列调度和灵活的任务模型。

[特性](#特性) • [快速开始](#快速开始) • [用法示例](#用法示例) • [架构](#架构)

</div>

## 📋 简介

ThreadLoom 提供了一个轻量级、高效的任务调度框架，具有以下核心特性：

- **优先级调度**：支持 Critical（最高）、High、Normal、Low 四个优先级
- **灵活的任务模型**：支持继承 `Task` 基类和 lambda 函数两种方式
- **线程池驱动**：可配置的固定大小工作线程池
- **安全的取消机制**：原子操作保证的线程安全任务取消
- **等待机制**：支持 `waitAll()` 阻塞等待所有任务完成

## ✨ 特性

### 优先级队列调度
任务按优先级分层执行，同优先级任务按提交顺序（FIFO）处理：
```
Critical > High > Normal > Low
```

### 两种任务提交方式

**1. 使用 Lambda 函数（快速方便）**
```cpp
scheduler.submit("task-name", [] { /* 任务逻辑 */ }, TaskPriority::High);
```

**2. 继承 Task 基类（复杂逻辑）**
```cpp
class MyTask : public Task {
    void execute() override { /* 任务逻辑 */ }
};
scheduler.submit(std::make_unique<MyTask>(...));
```

### 线程安全
- 使用原子操作管理任务状态
- 互斥量保护共享资源
- 条件变量协调线程间通信

## 🚀 快速开始

### 系统要求
- C++17 或更高版本
- CMake 3.14+
- 支持 Windows（MSVC）、Linux、macOS

### 编译

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

编译后可执行文件位于 `build/bin/ThreadLoom`（Windows）或 `build/bin/ThreadLoom`（Linux/macOS）

### 运行演示

```bash
./bin/ThreadLoom
```

输出示例：
```
=== ThreadLoom Scheduler Demo ===

Submitted 9 tasks. Waiting for completion...

[FnTask] Critical priority #1
[FnTask] High priority #1
[Task 8] compute-A  =>  7^2 = 49
[FnTask] Normal priority #1
[Task 9] compute-B  =>  13^2 = 169
[FnTask] Low priority #1
[Task 10] compute-C  =>  42^2 = 1764
...

=== All tasks completed. Total: 9 ===
```

## 📖 用法示例

### 基础示例：提交 Lambda 任务

```cpp
#include "scheduler/Scheduler.h"
using namespace ThreadLoom;

int main() {
    // 创建调度器，启动 4 个工作线程
    Scheduler scheduler(4);

    // 提交优先级任务
    scheduler.submit("task-1", [] {
        std::printf("执行任务 1\n");
    }, TaskPriority::High);

    scheduler.submit("task-2", [] {
        std::printf("执行任务 2\n");
    }, TaskPriority::Normal);

    // 等待所有任务完成
    scheduler.waitAll();

    return 0;
}
```

### 高级示例：自定义任务类

```cpp
class ComputeTask : public Task {
public:
    ComputeTask(std::string name, int value, TaskPriority prio)
        : Task(std::move(name), prio), value_(value) {}

    void execute() override {
        // 检查是否被取消
        if (isCancelled()) return;

        // 执行计算
        int result = value_ * value_;
        std::printf("[%s] %d^2 = %d\n", name().c_str(), value_, result);
    }

private:
    int value_;
};

// 使用示例
Scheduler scheduler(4);
scheduler.submit(std::make_unique<ComputeTask>("compute-A", 7, TaskPriority::High));
scheduler.submit(std::make_unique<ComputeTask>("compute-B", 13, TaskPriority::Normal));
scheduler.waitAll();
```

## 🏗️ 架构

### 系统设计

```
┌─────────────────────────────────────────┐
│          主应用线程                      │
│  submit() -> Scheduler::PriorityQueue   │
└────────────────┬────────────────────────┘
                 │
         ┌───────▼─────────┐
         │ Dispatcher 线程  │
         │ (优先级调度)     │
         └───────┬─────────┘
                 │
         ┌───────▼──────────────┐
         │   ThreadPool         │
         │ ┌──────────────────┐ │
         │ │ 工作线程 1       │ │
         │ ├──────────────────┤ │
         │ │ 工作线程 2       │ │
         │ ├──────────────────┤ │
         │ │ 工作线程 N       │ │
         │ └──────────────────┘ │
         └──────────────────────┘
```

### 核心组件

| 组件 | 文件 | 责任 |
|------|------|------|
| **Task** | `src/task/Task.h/cpp` | 任务基类、优先级、状态管理 |
| **ThreadPool** | `src/scheduler/ThreadPool.h/cpp` | 工作线程池、任务队列 |
| **Scheduler** | `src/scheduler/Scheduler.h/cpp` | 优先级调度器、分发器线程 |

### 线程模型

- **主应用线程**：调用 `submit()` 将任务加入优先级队列
- **分发器线程**：持续从队列取出最高优先级任务，分配给线程池
- **工作线程池**：执行 `task->execute()`，可并发处理多个任务

## 📦 目录结构

```
ThreadLoom/
├── src/
│   ├── main.cpp                 # 演示程序
│   ├── task/
│   │   ├── Task.h              # 任务基类定义
│   │   └── Task.cpp            # 任务实现
│   └── scheduler/
│       ├── Scheduler.h         # 调度器定义
│       ├── Scheduler.cpp       # 调度器实现
│       ├── ThreadPool.h        # 线程池定义
│       └── ThreadPool.cpp      # 线程池实现
├── CMakeLists.txt              # CMake 构建配置
└── README.md                   # 项目文档
```

## 🔌 API 文档

### Scheduler 类

```cpp
// 构造函数
explicit Scheduler(size_t threadCount = 0);

// 析构函数（自动调用 stop()）
~Scheduler();

// 提交自定义任务，返回任务 ID
Task::Id submit(std::unique_ptr<Task> task);

// 提交 Lambda 任务
Task::Id submit(std::string name,
                std::function<void()> fn,
                TaskPriority priority = TaskPriority::Normal);

// 等待所有任务完成
void waitAll();

// 获取已完成任务数量
size_t completedCount() const;

// 优雅关闭调度器
void stop();
```

### Task 基类

```cpp
// 构造函数
explicit Task(std::string name, TaskPriority priority = TaskPriority::Normal);

// 核心接口：子类必须实现
virtual void execute() = 0;

// 检查任务是否被取消
bool isCancelled() const;

// 请求取消任务
void cancel();

// 获取任务 ID
Id id() const;

// 获取任务名称
const std::string& name() const;

// 获取任务优先级
TaskPriority priority() const;

// 获取任务状态
TaskState state() const;
```

## 📊 任务优先级与状态

### 优先级等级

```cpp
enum class TaskPriority {
    Low      = 0,  // 低优先级
    Normal   = 1,  // 普通优先级（默认）
    High     = 2,  // 高优先级
    Critical = 3   // 最高优先级
};
```

### 任务生命周期

```
Pending -> Running -> Completed
                  \-> Failed
                  \-> Cancelled
```

## 🛠️ 开发与扩展

### 添加自定义任务

继承 `Task` 基类并实现 `execute()` 方法：

```cpp
class CustomTask : public Task {
public:
    CustomTask(std::string name, TaskPriority priority = TaskPriority::Normal)
        : Task(std::move(name), priority) {}

    void execute() override {
        // 检查取消状态
        if (isCancelled()) {
            return;
        }

        // 执行任务逻辑
        std::printf("Running %s\n", name().c_str());
    }
};
```

### 编译选项

CMakeLists.txt 配置了 MSVC 的以下选项：
- `/W4` - 警告级别 4
- `/MP` - 多处理器编译
- `/utf-8` - UTF-8 源和执行字符集

## 📝 代码示例

完整的演示程序见 `src/main.cpp`，展示了：
- 9 个不同优先级任务的提交
- Lambda 函数任务和自定义 ComputeTask
- 任务取消检查
- 等待所有任务完成

运行该程序可观察优先级调度效果。

## ⚙️ 构建系统

项目使用 CMake 构建，支持跨平台编译：

```bash
# 创建构建目录
mkdir build && cd build

# 配置（自动检测编译器）
cmake .. -DCMAKE_BUILD_TYPE=Release

# 编译
cmake --build . --config Release

# 运行
./bin/ThreadLoom
```

## 📄 许可证

本项目为学习和开发用途。

## 🤝 贡献

欢迎提交问题和改进建议！

---

**版本：** 1.0.0  
**语言：** C++17  
**平台：** Windows、Linux、macOS
