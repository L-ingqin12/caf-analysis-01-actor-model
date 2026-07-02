// =============================================================================
// CAF Actor Model Demo
// =============================================================================
//
// 概念演示：
//   1. 使用 actor_from_state 创建有状态 actor
//   2. 使用函数式方式创建 event_based_actor
//   3. 消息发送/接收: mail().send()
//   4. Request/Response 模式: mail().request().then() / .receive()
//   5. become/unbecome 动态行为切换
//   6. 链接 (link) 与监控 (monitor)
//   7. scoped_actor 在主线程中参与消息传递
//
// 架构：
//   caf_main
//     ├── spawn 一个计数器 actor (有状态，actor_from_state)
//     ├── spawn 一个回显 actor (函数式)
//     ├── spawn 一个监控 actor (链接 + request/response)
//     ├── 使用 scoped_actor 发送消息
//     └── 优雅关闭
// =============================================================================

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "caf/anon_mail.hpp"
#include "caf/actor_from_state.hpp"
#include "caf/actor_system.hpp"
#include "caf/caf_main.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/scoped_actor.hpp"
#include "caf/type_id.hpp"

using namespace caf;
using namespace std::literals;

// =============================================================================
// 1. 自定义类型和 Atom 定义
// =============================================================================

// 定义一个计数器操作的消息类型
struct count_event {
  int32_t old_value;
  int32_t new_value;
  std::string who;
};

// 注册类型和 atom
CAF_BEGIN_TYPE_ID_BLOCK(actor_model_demo, first_custom_type_id)

  CAF_ADD_TYPE_ID(actor_model_demo, (count_event))
  CAF_ADD_ATOM(actor_model_demo, increment_atom)
  CAF_ADD_ATOM(actor_model_demo, decrement_atom)
  CAF_ADD_ATOM(actor_model_demo, get_value_atom)
  CAF_ADD_ATOM(actor_model_demo, reset_atom)
  CAF_ADD_ATOM(actor_model_demo, ping_atom)
  CAF_ADD_ATOM(actor_model_demo, pong_atom)
  CAF_ADD_ATOM(actor_model_demo, hello_atom)
  CAF_ADD_ATOM(actor_model_demo, stop_atom)

CAF_END_TYPE_ID_BLOCK(actor_model_demo)

// 为 count_event 提供 inspect 函数（序列化支持）
template <class Inspector>
bool inspect(Inspector& f, count_event& x) {
  return f.object(x).fields(f.field("old_value", x.old_value),
                             f.field("new_value", x.new_value),
                             f.field("who", x.who));
}

// =============================================================================
// 2. 有状态 Actor: Counter
// =============================================================================
//
// 使用 actor_from_state 模式，状态类持有成员变量并通过 make_behavior()
// 返回初始行为。演示:
//   - become(keep_behavior, ...)/unbecome() 动态切换行为
//   - 向发送者返回响应
//   - delayed_send 定时自投递
// =============================================================================

struct counter_state {
  event_based_actor* self;
  std::string name;
  int32_t value = 0;

  counter_state(event_based_actor* selfptr, std::string n)
    : self(selfptr), name(std::move(n)) {
  }

  /// 返回初始行为：处于"活跃"模式，接受 increment/decrement/get_value/reset
  behavior make_behavior() {
    self->println("[{}] Counter started, initial value = {}", name, value);
    return active_behavior();
  }

  /// "活跃"模式的行为
  behavior active_behavior() {
    return {
      // -- increment: 自增，返回新值
      [this](increment_atom) -> int32_t {
        int32_t old = value;
        ++value;
        self->println("[{}] increment: {} -> {}", name, old, value);
        return value;
      },
      // -- decrement: 自减，返回新值
      [this](decrement_atom) -> int32_t {
        int32_t old = value;
        --value;
        self->println("[{}] decrement: {} -> {}", name, old, value);
        return value;
      },
      // -- get_value: 返回当前值
      [this](get_value_atom) -> int32_t {
        return value;
      },
      // -- reset: 重置为 0，切换到"确认"模式
      [this](reset_atom) {
        self->println("[{}] reset requested, need confirmation", name);
        self->become(keep_behavior, confirm_reset_behavior());
      },
    };
  }

  /// "确认重置"模式：需要先确认才能重置
  behavior confirm_reset_behavior() {
    return {
      // -- 确认重置：执行重置并切回活跃模式
      [this](reset_atom) {
        int32_t old = value;
        value = 0;
        self->println("[{}] reset confirmed: {} -> 0", name, old);
        self->unbecome();
      },
      // -- 反悔：不重置，直接切回活跃模式
      [this](get_value_atom) -> std::string {
        self->println("[{}] cancel reset, back to active", name);
        self->unbecome();
        self->mail(get_value_atom_v)
          .request(self, 5s)
          .then([this](int32_t v) {
            self->println("[{}] value after cancel = {}", name, v);
          });
        return name + " cancel reset, value unchanged";
      },
    };
  }

  ~counter_state() {
    self->println("[{}] Counter destroyed, final value = {}", name, value);
  }
};

// =============================================================================
// 3. 函数式 Actor: Echo (event_based_actor)
// =============================================================================
//
// 演示函数式创建 actor:
//   - 使用 request/response 模式处理消息
//   - 使用 set_idle_handler 设置空闲超时
// =============================================================================

behavior echo_actor(event_based_actor* self) {
  self->println("[Echo] Echo actor started, waiting for messages");

  // 设置空闲重复超时：每 5 秒打印一次
  self->set_idle_handler(5s, strong_ref, repeat, [self] {
    self->println("[Echo] (idle) waiting ...");
  });

  return {
    // -- 问候：返回问候语（演示 request/response 的响应端）
    [self](hello_atom, const std::string& who) -> std::string {
      self->println("[Echo] hello from {}", who);
      return "Hello, " + who + "!";
    },
    // -- ping 响应 pong
    [](ping_atom) -> std::string {
      return "pong";
    },
    // -- 任意字符串消息回显
    [self](const std::string& msg) -> std::string {
      self->println("[Echo] echoing '{}'", msg);
      return msg;
    },
    // -- 超时处理
    after(10s) >> [self] {
      self->println("[Echo] timeout after 10s, shutting down");
      self->quit();
    },
  };
}

// =============================================================================
// 4. 监控 Actor: 演示 request/response 和链接
// =============================================================================
//
// 发送多个 request 到 counter，收集结果。
// 链接到 counter，当 counter 退出时收到通知。
// =============================================================================

behavior monitor_actor(event_based_actor* self, actor counter) {
  self->println("[Monitor] Monitor started, will query counter");

  // 链接到 counter：如果 counter 异常退出，monitor 也会收到退出消息
  self->link_to(counter);

  // 发送一系列请求，使用 .then() 异步处理每个响应
  self->mail(increment_atom_v).request(counter, 5s)
    .then([self](int32_t v) {
      self->println("[Monitor] increment response: value = {}", v);
    });

  self->mail(increment_atom_v).request(counter, 5s)
    .then([self](int32_t v) {
      self->println("[Monitor] increment response: value = {}", v);
    });

  self->mail(get_value_atom_v).request(counter, 5s)
    .then([self](int32_t v) {
      self->println("[Monitor] get_value response: value = {}", v);
    });

  // 发送一个无法处理的消息，测试超时
  self->mail(stop_atom_v).request(counter, 2s)
    .then(
      [self] {
        self->println("[Monitor] unexpected: stop_atom was handled");
      },
      [self](const error& err) {
        self->println("[Monitor] stop_atom request failed (expected): {}", err);
      });

  return {
    // 当 counter 退出时收到 down_msg
    [self](const down_msg& dm) {
      self->println("[Monitor] counter down: reason = {}", dm.reason);
      self->quit();
    },
  };
}

// =============================================================================
// 5. Main
// =============================================================================

void caf_main(actor_system& sys) {
  sys.println("============================================================");
  sys.println("  CAF Actor Model Demo");
  sys.println("============================================================");

  // ------------------------------------------------------------------------
  // 5.1 创建 Actors
  // ------------------------------------------------------------------------
  sys.println("\n--- [1] Spawning Actors ---");

  // 使用 actor_from_state 创建有状态 counter actor
  auto counter = sys.spawn(actor_from_state<counter_state>, std::string{"Counter-A"});

  // 函数式创建 echo actor
  auto echo = sys.spawn(echo_actor);

  // 创建 monitor actor（传入 counter 的引用）
  auto monitor = sys.spawn(monitor_actor, actor{counter});

  // 等待 actors 初始化
  std::this_thread::sleep_for(200ms);

  // ------------------------------------------------------------------------
  // 5.2 scoped_actor: 主线程参与消息传递
  // ------------------------------------------------------------------------
  sys.println("\n--- [2] scoped_actor ---");

  // scoped_actor 是一个 blocking_actor，让主线程像 actor 一样收发消息
  scoped_actor self{sys};

  // -- send: 发送问候消息
  self->println("[Main] sending hello to echo");
  self->mail(hello_atom_v, std::string{"MainThread"}).send(echo);

  // -- request + receive: 阻塞等待响应
  self->println("[Main] requesting ping from echo");
  self->mail(ping_atom_v)
    .request(echo, 5s)
    .receive(
      [&self](const std::string& response) {
        self->println("[Main] echo response: '{}'", response);
      },
      [&self](const error& err) {
        self->println("[Main] echo error: {}", err);
      });

  // -- anon_mail: 匿名发送（不关联 sender）
  self->println("[Main] sending anonymous message to echo");
  anon_mail(std::string{"anonymous ping"}).send(echo);

  // -- request + await: 在独立 actor 中演示顺序等待
  self->println("[Main] spawning actor to demonstrate request().await()");
  sys.spawn([counter](event_based_actor* eb_self) {
    // await 保证响应按序处理（不同于 then 的并发回调）
    eb_self->mail(increment_atom_v).request(counter, 5s)
      .await([eb_self](int32_t v) {
        eb_self->println("[await] first increment result: {}", v);
      });
    eb_self->mail(increment_atom_v).request(counter, 5s)
      .await([eb_self](int32_t v) {
        eb_self->println("[await] second increment result: {}", v);
      });
    eb_self->println("[await] all sequential requests completed, quitting");
    eb_self->quit();
  });

  // ------------------------------------------------------------------------
  // 5.3 操作 counter actor
  // ------------------------------------------------------------------------
  self->println("\n--- [3] Counter Operations ---");

  // 获取当前值
  self->mail(get_value_atom_v)
    .request(counter, 5s)
    .receive(
      [&self](int32_t v) {
        self->println("[Main] initial counter value = {}", v);
      },
      [&self](const error& err) {
        self->println("[Main] get_value error: {}", err);
      });

  // 自增
  self->mail(increment_atom_v)
    .request(counter, 5s)
    .receive(
      [&self](int32_t v) {
        self->println("[Main] after increment: {}", v);
      },
      [&self](const error& err) {
        self->println("[Main] increment error: {}", err);
      });

  // 连续两次自增（验证状态保持）
  for (int i = 0; i < 2; ++i) {
    self->mail(increment_atom_v)
      .request(counter, 5s)
      .receive(
        [&self, i](int32_t v) {
          self->println("[Main] increment #{} result: {}", i + 1, v);
        },
        [&self](const error& err) {
          self->println("[Main] increment error: {}", err);
        });
  }

  // 请求重置（进入确认模式）
  self->mail(reset_atom_v).send(counter);

  // 此时 counter 处于 confirm_reset_behavior，发送 reset 确认重置
  self->mail(reset_atom_v).request(counter, 5s)
    .receive(
      [&self] {
        self->println("[Main] reset confirmed");
      },
      [&self](const error& err) {
        self->println("[Main] reset error: {}", err);
      });

  // 验证重置后值为 0
  self->mail(get_value_atom_v)
    .request(counter, 5s)
    .receive(
      [&self](int32_t v) {
        self->println("[Main] value after reset = {}", v);
      },
      [&self](const error& err) {
        self->println("[Main] get_value error: {}", err);
      });

  // ------------------------------------------------------------------------
  // 5.4 发送字符串到 echo（演示泛型消息处理）
  // ------------------------------------------------------------------------
  self->println("\n--- [4] Echo Communication ---");
  self->mail(std::string{"Hello from Main!"}).send(echo);

  // 使用 request 发送字符串并接收回显
  self->mail(std::string{"Can you echo this?"})
    .request(echo, 5s)
    .receive(
      [&self](const std::string& response) {
        self->println("[Main] echoed back: '{}'", response);
      },
      [&self](const error& err) {
        self->println("[Main] echo error: {}", err);
      });

  // ------------------------------------------------------------------------
  // 5.5 关闭
  // ------------------------------------------------------------------------
  self->println("\n--- [5] Clean Shutdown ---");

  // 发送退出消息
  self->send_exit(counter, exit_reason::user_shutdown);
  self->send_exit(echo, exit_reason::user_shutdown);

  // 等待一下让 actors 处理退出
  std::this_thread::sleep_for(200ms);

  // ------------------------------------------------------------------------
  // 5.6 配置演示
  // ------------------------------------------------------------------------
  sys.println("\n--- [6] Configuration Demo ---");
  demo_actor_system_config(sys);

  sys.println("\n============================================================");
  sys.println("  Demo Complete");
  sys.println("============================================================");
}


// =============================================================================
// 6. actor_system_config 演示
// =============================================================================
void demo_actor_system_config(actor_system& sys) {
  sys.println("[Config] Creating custom actor_system_config...");

  // 创建自定义配置对象演示各种设置项
  actor_system_config cfg;

  // -- 调度器设置
  cfg.set("caf.scheduler.max-threads", size_t{4});
  cfg.set("caf.scheduler.policy", std::string{"stealing"});
  cfg.set("caf.scheduler.max-throughput", size_t{500});

  // -- Logger 设置
  cfg.set("caf.logger.console.verbosity", std::string{"info"});
  cfg.set("caf.logger.console.colored", bool{true});

  // -- Work stealing 调度器参数
  cfg.set("caf.work-stealing.aggressive-poll-attempts", size_t{50});
  cfg.set("caf.work-stealing.aggressive-steal-interval", size_t{5});
  cfg.set("caf.work-stealing.moderate-poll-attempts", size_t{200});
  cfg.set("caf.work-stealing.moderate-steal-interval", size_t{3});
  cfg.set("caf.work-stealing.moderate-sleep-duration", 50us);
  cfg.set("caf.work-stealing.relaxed-steal-interval", size_t{1});
  cfg.set("caf.work-stealing.relaxed-sleep-duration", 10ms);

  // 输出配置摘要
  sys.println("[Config] 调度器策略 = stealing, max-threads = 4");
  sys.println("[Config] Logger 级别 = info, 彩色输出 = true");
  sys.println("[Config] 以上参数可通过 caf-application.conf 文件或命令行覆盖");
  sys.println("[Config] 配置文件路径: caf-application.conf");
}


CAF_MAIN(id_block::actor_model_demo)
