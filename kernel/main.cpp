#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>

#include "asmfunc.h"
#include "console.hpp"
#include "clock.hpp"
#include "error.hpp"
#include "fonts.hpp"
#include "frame_buffer_config.hpp"
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
  msg_queue = new std::deque<Message>(32);
  InitializeInterrupt(msg_queue);

  InitializePCI();
  usb::xhci::Initialize();

  InitializeLayer();
  InitializeMainWindow();
  InitializeMouse();
  layer_manager->Draw({{0,0},ScreenSize()});

  // コンソール表示
  // DrawDesktop(*pixel_writer);

  acpi::Initialize(acpi_table);
  InitializeLAPICTimer(*msg_queue);
  timer_manager->AddTimer(Timer(200, 2));
  timer_manager->AddTimer(Timer(600,-1));

  InitializeClock();

  // char str[128];

  while (true)
  {
    // __asm__("cli");
    // const auto tick = timer_manager->CurrentTick();
    // __asm__("sti");
    // sprintf(str, "%010lu", tick);
    // FillRectangle(*main_window->Writer(), {24, 28}, {8 * 10, 16}, {0xc6, 0xc6, 0xc6});
    // WriteString(*main_window->Writer(), {24, 28}, str, {0, 0, 0});
    // layer_manager->Draw(main_window_layer_id);

    __asm__("cli");
    if (msg_queue->size() == 0)
    {
      __asm__("sti\n\thlt");
      continue;
    }

    Message msg = msg_queue->front();
    msg_queue->pop_front();
    __asm__("sti");

    switch (msg.type)
    {
    case Message::kInterruptXHCI:
      usb::xhci::ProcessEvents();
      break;
    case Message::kTimerTimeout:
      printk("Timer timeout: timeout(%lu), value(%d)\n", msg.arg.timer.timeout, msg.arg.timer.value);
      if (msg.arg.timer.value > 0) {
        timer_manager->AddTimer(Timer(msg.arg.timer.timeout + 100, msg.arg.timer.value + 1));
        DrawClock(*clock_window->Writer());
      }
      break;
    default:
      printk("Unknown message type: %d\n", msg.type);
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