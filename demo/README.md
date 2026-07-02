# CAF Actor Model Demo

## 概述

本 Demo 演示 CAF Actor Model 的核心概念，包括：

1. **有状态 Actor** — 使用 `actor_from_state` 模式，状态类持有变量
2. **函数式 Actor** — 使用 `event_based_actor` 和函数式接口
3. **消息发送** — `mail().send()` 异步发送
4. **Request/Response** — `mail().request().then()` / `.receive()` 模式
5. **动态行为切换** — `become()` / `unbecome()` 切换行为状态
6. **Actor 链接** — `link_to()` 建立双向生命周期通知
7. **scoped_actor** — 主线程作为 blocking_actor 收发消息
8. **Actor 生命周期管理** — `send_exit` 优雅关闭

## 架构说明

```
caf_main
  ├── counter_state (actor_from_state)
  │     ├── active_behavior: increment / decrement / get_value
  │     └── confirm_reset_behavior: 需确认后才能重置
  ├── echo_actor (event_based_actor)
  │     ├── hello_atom + string → 响应问候
  │     ├── ping_atom → pong
  │     ├── string → 回显
  │     └── after(10s) → 超时退出
  ├── monitor_actor (event_based_actor + link_to)
  │     └── 向 counter 发送多个 request，收集响应
  └── scoped_actor (main thread)
        └── 发送消息并接收响应
```

## 编译与运行

### 前置条件

- CMake >= 3.19
- C++17 编译器
- Git（用于 FetchContent 拉取 CAF）

### 编译步骤

```bash
cd /root/caf-analysis-output/01-actor-model/demo
mkdir -p build && cd build
cmake ..
cmake --build .
```

### 运行

```bash
./actor_model_demo
```

### 预期输出

程序将按顺序输出各部分的运行结果，大致如下：

```
============================================================
  CAF Actor Model Demo
============================================================

--- [1] Spawning Actors ---
[Counter-A] Counter started, initial value = 0
[Echo] Echo actor started, waiting for messages
[Monitor] Monitor started, will query counter
...
```

## 代码结构

| 部分 | 功能 | 对应代码 |
|------|------|----------|
| 类型与 Atom | 定义自定义消息类型和 Atom | `CAF_BEGIN_TYPE_ID_BLOCK` |
| counter_state | 有状态计数器 actor | `struct counter_state` |
| echo_actor | 回显 actor（函数式） | `behavior echo_actor(...)` |
| monitor_actor | 监控 actor + request | `behavior monitor_actor(...)` |
| caf_main | 入口：创建 actors 并交互 | `void caf_main(...)` |
