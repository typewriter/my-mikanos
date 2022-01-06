#include <array>
// #include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <optional>

#include "asmfunc.h"
#include "console.hpp"
#include "clock.hpp"
#include "error.hpp"
#include "fonts.hpp"
#include "frame_buffer_config.hpp"
#include "keyboard.hpp"
#include "graphics.hpp"
#include "interrupt.hpp"
#include "layer.hpp"
#include "memory_manager.hpp"
#include "memory_map.hpp"
#include "mouse.hpp"
#include "paging.hpp"
#include "pci.hpp"
#include "queue.hpp"
#include "segment.hpp"
#include "task.hpp"
#include "timer.hpp"
#include "usb/classdriver/mouse.hpp"
#include "usb/xhci/xhci.hpp"
#include "window.hpp"
#include "acpi.hpp"

// void *operator new(size_t size, void *buf)
// {
//   return buf;
// }

void operator delete(void *obj) noexcept {}

void SwitchEhci2Xhci(const pci::Device &xhc_dev)
{
  bool intel_ehc_exist = false;
  for (int i = 0; i < pci::num_device; ++i)
  {
    if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x20u) /* EHCI */ &&
        0x8086 == pci::ReadVendorId(pci::devices[i]))
    {
      intel_ehc_exist = true;
      break;
    }
  }
  if (!intel_ehc_exist)
  {
    return;
  }

  uint32_t superspeed_ports = pci::ReadConfReg(xhc_dev, 0xdc);
  pci::WriteConfReg(xhc_dev, 0xd8, superspeed_ports);
  uint32_t ehci2xhci_ports = pci::ReadConfReg(xhc_dev, 0xd4);
  pci::WriteConfReg(xhc_dev, 0xd0, ehci2xhci_ports);
  printk("SwitchEhci2Xhci: SS = %02, xHCI = %02x\n", superspeed_ports,
         ehci2xhci_ports);
}

// ------------------------------------------------------
// テキストウィンドウ表示処理
// ------------------------------------------------------

std::shared_ptr<ToplevelWindow> text_window;
unsigned int text_window_layer_id;
void InitializeTextWindow() {
  const int win_w = 160;
  const int win_h = 52;

  text_window = std::make_shared<ToplevelWindow>(
    win_w, win_h, GetFrameBufferConfig().pixel_format, "Textbox Test");
  DrawTextbox(*text_window->InnerWriter(), {0, 0}, text_window->InnerSize());

  text_window_layer_id = layer_manager->NewLayer()
                             .SetWindow(text_window)
                             .SetDraggable(true)
                             .Move({350, 200})
                             .ID();

  layer_manager->UpDown(text_window_layer_id, std::numeric_limits<int>::max());
}

int text_window_index;
void DrawTextCursor(bool visible) {
  const auto color = visible ? ToColor(0) : ToColor(0xffffff);
  const auto pos = Vector2D<int>{4+8*text_window_index, 5};
  FillRectangle(*text_window->InnerWriter(), pos, {7, 15}, color);
}

void InputTextWindow(char c) {
  if (c == 0) {
    return;
  }

  auto pos = []() { return Vector2D<int>{4 + 8*text_window_index, 6}; };

  const int max_chars = (text_window->Width() - 16) / 8;
  if (c == '\b' && text_window_index > 0) {
    DrawTextCursor(false);
    --text_window_index;
    FillRectangle(*text_window->InnerWriter(), pos(), {8,16}, ToColor(0xffffff));
    DrawTextCursor(true);
  } else if(c >= ' ' && text_window_index < max_chars) {
    DrawTextCursor(false);
    WriteAscii(*text_window->InnerWriter(), pos().x, pos().y, c, ToColor(0));
    ++text_window_index;
    DrawTextCursor(true);
  }
  layer_manager->Draw(text_window_layer_id);
}

// ------------------------------------------------------

std::shared_ptr<ToplevelWindow> task_b_window;
unsigned int task_b_window_layer_id;
void InitializeTaskBWindow() {
  task_b_window = std::make_shared<ToplevelWindow>(
    160, 52, GetFrameBufferConfig().pixel_format, "TaskB Window");
  // DrawWindow(*task_b_window->Writer(), "TaskB Window");

  task_b_window_layer_id = layer_manager->NewLayer()
    .SetWindow(task_b_window)
    .SetDraggable(true)
    .Move({100, 100})
    .ID();

  layer_manager->UpDown(task_b_window_layer_id, std::numeric_limits<int>::max());
}

void TaskB(uint64_t task_id, int64_t data) {
  printk("TaskB: task_id=%lu, data=%lx\n", task_id, data);
  char str[128];
  int count = 0;

  __asm__("cli");
  Task& task = task_manager->CurrentTask();
  __asm__("sti");

  while(true) {
    ++count;
    sprintf(str, "%010d", count);
    FillRectangle(*task_b_window->InnerWriter(), {0, 0}, task_b_window->InnerSize(), {0xc6, 0xc6, 0xc6});
    WriteString(*task_b_window->InnerWriter(), {0, 0}, str, {0, 0, 0});

    Message msg{Message::kLayer, task_id};
    msg.arg.layer.layer_id = task_b_window_layer_id;
    msg.arg.layer.op = LayerOperation::Draw;
    __asm__("cli");
    task_manager->SendMessage(1, msg);
    __asm__("sti");

    while (true)
    {
      __asm__("cli");
      auto msg = task.ReceiveMessage();
      if (!msg)
      {
        task.Sleep();
        __asm__("sti");
        continue;
      }

      if (msg->type == Message::kLayerFinish)
      {
        break;
      }
    }
  }
}

// char mouse_cursor_buf[sizeof(MouseCursor)];
// MouseCursor *mouse_cursor;
usb::xhci::Controller *xhc;

alignas(16) uint8_t kernel_main_stack[1024 * 1024];

extern "C" void KernelMainNewStack(
    const FrameBufferConfig &frame_buffer_config_ref,
    const MemoryMap &memory_map_ref,
    const acpi::RSDP& acpi_table)
{
  MemoryMap memory_map{memory_map_ref};

  InitializeGraphics(frame_buffer_config_ref);
  InitializeConsole();

  InitializeSegmentation();
  InitializePaging();
  InitializeMemoryManager(memory_map);
  InitializeInterrupt();

  InitializePCI();

  InitializeLayer();
  InitializeMainWindow();
  InitializeTextWindow();
  InitializeTaskBWindow();
  layer_manager->Draw({{0,0},ScreenSize()});
  active_layer->Activate(task_b_window_layer_id);

  // コンソール表示
  // DrawDesktop(*pixel_writer);

  acpi::Initialize(acpi_table);
  InitializeLAPICTimer();
  timer_manager->AddTimer(Timer(200, 2));
  timer_manager->AddTimer(Timer(600,-1));

  InitializeClock();

  const int kTextboxCursorTimer = -5;
  const int kTimer500ms = static_cast<int>(kTimerFreq * 0.5);
  __asm__("cli");
  timer_manager->AddTimer(Timer{kTimer500ms, kTextboxCursorTimer});
  __asm__("sti");
  bool textbox_cursor_visible = false;


  // // Create TaskB context
  // std::vector<uint64_t> task_b_stack(1024);
  // uint64_t task_b_stack_end = reinterpret_cast<uint64_t>(&task_b_stack[1024]);

  // memset(&task_b_ctx, 0, sizeof(task_b_ctx));
  // task_b_ctx.rip = reinterpret_cast<uint64_t>(TaskB);
  // task_b_ctx.rdi = 1;
  // task_b_ctx.rsi = 43;

  // task_b_ctx.cr3 = GetCR3();
  // task_b_ctx.rflags = 0x202;
  // task_b_ctx.cs = kKernelCS;
  // task_b_ctx.ss = kKernelSS;
  // task_b_ctx.rsp = (task_b_stack_end & ~0xflu) - 8;

  // // MXCSR のすべての例外をマスクする
  // *reinterpret_cast<uint32_t*>(&task_b_ctx.fxsave_area[24]) = 0x1f80;

  InitializeTask();
  Task& main_task = task_manager->CurrentTask();
  const uint64_t taskb_id = 
    task_manager->NewTask()
    .InitContext(TaskB, 45)
    .Wakeup()
    .ID();

  usb::xhci::Initialize();
  InitializeMouse();
  InitializeKeyboard();

  char str[128];

  while (true)
  {
    __asm__("cli");
    const auto tick = timer_manager->CurrentTick();
    __asm__("sti");
    sprintf(str, "%010lu", tick);
    FillRectangle(*main_window->InnerWriter(), {0, 0}, main_window->InnerSize(), {0xc6, 0xc6, 0xc6});
    WriteString(*main_window->InnerWriter(), {0, 0}, str, {0, 0, 0});
    layer_manager->Draw(main_window_layer_id);

    __asm__("cli");
    std::optional<Message> msg = main_task.ReceiveMessage();
    if (!msg)
    {
      main_task.Sleep();
      __asm__("sti");
      continue;
    }

    // __asm__("cli");
    // if (msg_queue->size() == 0)
    // {
    //   __asm__("sti\n\thlt");
    //   // __asm__("sti");
    //   // printk("Switch context to TaskB...\n");
    //   // SwitchContext(&task_b_ctx, &task_a_ctx);
    //   // printk("Restore context TaskMain\n");
    //   continue;
    // }

    // Message msg = msg_queue->front();
    // msg_queue->pop_front();
    __asm__("sti");

    switch (msg->type)
    {
    case Message::kInterruptXHCI:
      usb::xhci::ProcessEvents();
      break;
    case Message::kTimerTimeout:
      // printk("Timer timeout: timeout(%lu), value(%d)\n", msg->arg.timer.timeout, msg->arg.timer.value);
      if (msg->arg.timer.value > 0) {
        timer_manager->AddTimer(Timer(msg->arg.timer.timeout + 100, msg->arg.timer.value + 1));
        DrawClock(*clock_window->Writer());
      }
      if (msg->arg.timer.value == kTextboxCursorTimer) {
        __asm__("cli");
        timer_manager->AddTimer(
            Timer{msg->arg.timer.timeout + kTimer500ms, kTextboxCursorTimer});
        __asm__("sti");
        textbox_cursor_visible = !textbox_cursor_visible;
        DrawTextCursor(textbox_cursor_visible);
        layer_manager->Draw(text_window_layer_id);
      }
      break;
    case Message::kKeyPush:
      // if (msg.arg.keyboard.ascii != 0) {
      //   printk("%c", msg.arg.keyboard.ascii);
      // }
      if (auto act = active_layer->GetActive(); act == text_window_layer_id)
      {
        InputTextWindow(msg->arg.keyboard.ascii);
      } else if (act == task_b_window_layer_id)
      {
        if (msg->arg.keyboard.ascii == 's')
        {
          printk("Sleep TaskB: %s\n", task_manager->Sleep(taskb_id).Name());
        }
        else if (msg->arg.keyboard.ascii == 'w')
        {
          printk("Wakeup TaskB: %s\n", task_manager->Wakeup(taskb_id).Name());
        }
      } else
      {
        printk("Key not handled: keycode %02x, ascii %02x\n", msg->arg.keyboard.keycode, msg->arg.keyboard.ascii);
      }

      break;
    case Message::kLayer:
      ProcessLayerMessage(msg.value());
      __asm__("cli");
      task_manager->SendMessage(msg->src_task, Message{Message::kLayerFinish});
      __asm__("sti");
      break;
    default:
      printk("Unknown message type: %d\n", msg->type);
    }
  }

  while (1)
    __asm__("hlt");
}

extern "C" void __cxa_pure_virtual()
{
  while (1)
    __asm__("hlt");
}