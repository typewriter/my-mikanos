#pragma once

#include <array>
#include <vector>
#include <memory>
#include <deque>
#include <optional>

#include "error.hpp"
#include "interrupt.hpp"

struct TaskContext
{
  uint64_t cr3, rip, rflags, reserved1;            // offset 0x00
  uint64_t cs, ss, fs, gs;                         // offset 0x20
  uint64_t rax, rbx, rcx, rdx, rdi, rsi, rsp, rbp; // offset 0x40
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;   // offset 0x80
  std::array<uint8_t, 512> fxsave_area;            // offset 0xc0
} __attribute__((packed));

alignas(16) extern TaskContext task_b_ctx, task_a_ctx;

const int kDefaultLevel = 2;
using TaskFunc = void(uint64_t, int64_t);

extern class TaskManager;

class Task
{
public:
  static const size_t kDefaultStackBytes = 4096;
  Task(uint64_t id);
  Task &InitContext(TaskFunc *f, int64_t data);
  TaskContext &Context();
  uint64_t ID() const;
  int Level() const;
  bool Running() const;
  Task &Sleep();
  Task &Wakeup();
  std::optional<Message> ReceiveMessage();
  void SendMessage(const Message &msg);

private:
  uint64_t id_;
  std::vector<uint64_t> stack_;
  alignas(16) TaskContext context_;
  std::deque<Message> msgs_;

  unsigned int level_{kDefaultLevel};
  bool running_{false};

  Task &SetLevel(int level)
  {
    level_ = level;
    return *this;
  }
  Task &SetRunning(bool running)
  {
    running_ = running;
    return *this;
  }

  friend TaskManager;
};

class TaskManager
{
public:
  // level: 0 = lowest, kMaxLevel = highest
  static const int kMaxLevel = 3;

  TaskManager();
  Task &NewTask();
  void SwitchTask(const TaskContext &current_ctx);

  Task &CurrentTask();
  Task *RotateCurrentRunQueue(bool current_sleep);
  Error SendMessage(uint64_t id, const Message &msg);

  void Sleep(Task *task);
  Error Sleep(uint64_t id);
  void Wakeup(Task *task, int level = -1);
  Error Wakeup(uint64_t id, int level = -1);

private:
  std::vector<std::unique_ptr<Task>> tasks_{};
  uint64_t latest_id_{0};
  std::array<std::deque<Task *>, kMaxLevel + 1> running_{};
  int current_level_{kMaxLevel};
  bool level_changed_{false};

  void ChangeLevelRunning(Task *task, int level);
};

extern TaskManager *task_manager;

void InitializeTask();