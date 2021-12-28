#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "asmfunc.h"
#include "console.hpp"
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

// void *operator new(size_t size, void *buf)
// {
//   return buf;
// }

void operator delete(void *obj) noexcept {}

char console_buf[sizeof(Console)];
Console *console;

int printk(const char *format, ...)
{
  va_list ap;
  int result;
  char s[1024];

  va_start(ap, format);
  result = vsprintf(s, format, ap);
  va_end(ap);

  StartLAPICTimer();
  console->PutString(s);
  auto elapsed = LAPICTimerElapsed();
  StopLAPICTimer();

  sprintf(s, "[%09d]", elapsed);
  console->PutString(s);
  return result;
}

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
unsigned int mouse_layer_id;
Vector2D<int> mouse_position;

void MouseObserver(uint8_t buttons, int8_t displacement_x, int8_t displacement_y)
{
  static unsigned int mouse_drag_layer_id = 0;
  static uint8_t previous_buttons = 0;

  const auto oldpos = mouse_position;
  auto newpos = mouse_position + Vector2D<int>{displacement_x, displacement_y};
  // FIXME: なおす (day11a)
  // newpos = ElementMin(newpos, screen_size + Vector2D<int>{-1, -1});
  mouse_position = ElementMax(newpos, {0, 0});

  const auto posdiff = mouse_position - oldpos;

  layer_manager->Move(mouse_layer_id, mouse_position);

  const bool previous_left_pressed = (previous_buttons & 0x01);
  const bool left_pressed = (buttons & 0x01);

  if (!previous_left_pressed && left_pressed)
  {
    // ボタンが押された
    auto layer = layer_manager->FindLayerByPosition(mouse_position, mouse_layer_id);
    if (layer && layer->IsDraggable())
    {
      mouse_drag_layer_id = layer->ID();
    }
  }
  else if (previous_left_pressed && left_pressed)
  {
    // 押された状態が続いている
    if (mouse_drag_layer_id > 0)
    {
      layer_manager->MoveRelative(mouse_drag_layer_id, posdiff);
    }
  }
  else if (previous_left_pressed && !left_pressed)
  {
    mouse_drag_layer_id = 0;
  }

  previous_buttons = buttons;
}

char memory_manager_buf[sizeof(BitmapMemoryManager)];
BitmapMemoryManager *memory_manager;

void NotifyEndOfInterrupt()
{
  volatile auto end_of_interrupt = reinterpret_cast<uint32_t *>(0xfee000b0);
  *end_of_interrupt = 0;
}

struct Message
{
  enum Type
  {
    kInterruptXHCI,
  } type;
};

ArrayQueue<Message> *main_queue;

__attribute__((interrupt)) void IntHandlerXHCI(InterruptFrame *frame)
{
  main_queue->Push(Message{Message::kInterruptXHCI});
  NotifyEndOfInterrupt();
}

usb::xhci::Controller *xhc;

alignas(16) uint8_t kernel_main_stack[1024 * 1024];

extern "C" void KernelMainNewStack(
    const FrameBufferConfig &frame_buffer_config_ref,
    const MemoryMap &memory_map_ref)
{
  MemoryMap memory_map{memory_map_ref};

  InitializeGraphics(frame_buffer_config_ref);
  InitializeConsole();

  // InitializeSegmentation();
  // InitializePaging();
  // InitializeMemorymanager(memory_map);
  // ::main_queue = new std::deque<Message>(32);
  // InitializeInterrupt(main_queue);

  // InitializePCI();
  // usb:xhci::Initialize();

  // INitializeLayer();
  // InitializeMainWindow();
  // InitializeMouse();
  // layer_manager->Draw({{0,0},ScreenSize()});

  // コンソール表示
  // DrawDesktop(*pixel_writer);

  console = new (console_buf) Console{kDesktopFGColor, kDesktopBGColor};
  console->SetWriter(GetPixelWriter());

  printk("Konnichiwa!\n");
  InitializeLAPICTimer();

  // メモリ設定
  SetupSegments();

  const uint16_t kernel_cs = 1 << 3;
  const uint16_t kernel_ss = 2 << 3;
  SetDSAll(0);
  SetCSSS(kernel_cs, kernel_ss);

  SetupIdentityPageTable();

  // メモリマネージャ
  ::memory_manager = new (memory_manager_buf) BitmapMemoryManager;

  const auto memory_map_base = reinterpret_cast<uintptr_t>(memory_map.buffer);
  uintptr_t available_end = 0;
  for (uintptr_t iter = memory_map_base;
       iter < memory_map_base + memory_map.map_size;
       iter += memory_map.descriptor_size)
  {
    auto desc = reinterpret_cast<const MemoryDescriptor *>(iter);
    if (available_end < desc->physical_start)
    {
      memory_manager->MarkAllocated(
          FrameID{available_end / kBytesPerFrame},
          (desc->physical_start - available_end) / kBytesPerFrame);
    }

    const auto physical_end =
        desc->physical_start + desc->number_of_pages * kUEFIPageSize;
    if (IsAvailable(static_cast<MemoryType>(desc->type)))
    {
      available_end = physical_end;
    }
    else
    {
      memory_manager->MarkAllocated(
          FrameID{desc->physical_start / kBytesPerFrame},
          desc->number_of_pages * kUEFIPageSize / kBytesPerFrame);
    }
  }
  memory_manager->SetMemoryRange(FrameID{1},
                                 FrameID{available_end / kBytesPerFrame});

  if (auto err = InitializeHeap(*memory_manager))
  {
    printk("Failed to allocate pages: %s at %s:%d\n", err.Name(), err.File(),
           err.Line());
    exit(1);
  }

  // FillRectangle(*pixel_writer,
  //               {0, 0},
  //               {kFrameWidth, kFrameHeight - 32},
  //               kDesktopBGColor);
  // FillRectangle(*pixel_writer,
  //               {0, kFrameHeight - 32},
  //               {kFrameWidth, 32},
  //               {0, 0, 0});
  // FillRectangle(*pixel_writer,
  //               {4, kFrameHeight - 28},
  //               {24, 24},
  //               {255, 255, 255});
  // DrawRectangle(*pixel_writer,
  //               {kFrameWidth - 64, kFrameHeight - 28},
  //               {60, 24},
  //               {128, 128, 128});

  // WriteString(*pixel_writer, kFrameWidth - 56, kFrameHeight - 24, "22:30",
  // {255, 255, 255});

  // USB デバイスの検索、マウス操作反映
  auto err = pci::ScanAllBus();
  printk("ScanAllBus: %s\n", err.Name());

  for (int i = 0; i < pci::num_device; ++i)
  {
    const auto &dev = pci::devices[i];
    auto vendor_id = pci::ReadVendorId(dev.bus, dev.device, dev.function);
    auto class_code = pci::ReadClassCode(dev.bus, dev.device, dev.function);
    printk("%d.%d.%d: vend %04x, class %08x, head %02x\n", dev.bus, dev.device,
           dev.function, vendor_id, class_code, dev.header_type);
  }

  pci::Device *xhc_dev = nullptr;
  for (int i = 0; i < pci::num_device; ++i)
  {
    if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x30u))
    {
      xhc_dev = &pci::devices[i];

      if (0x8086 ==
          pci::ReadVendorId(xhc_dev->bus, xhc_dev->device, xhc_dev->function))
      {
        break;
      }
    }
  }

  if (xhc_dev)
  {
    printk("xHC has been found: %d.%d.%d\n", xhc_dev->bus, xhc_dev->device,
           xhc_dev->function);
  }

  const uint16_t cs = GetCS();
  SetIDTEntry(idt[InterruptVector::kXHCI],
              MakeIDTAttr(DescriptorType::kInterruptGate, 0),
              reinterpret_cast<uint64_t>(IntHandlerXHCI), cs);
  LoadIDT(sizeof(idt) - 1, reinterpret_cast<uintptr_t>(&idt[0]));

  const uint8_t bsp_local_apic_id =
      *reinterpret_cast<const uint32_t *>(0xfee00020) >> 24;
  pci::ConfigureMSIFixedDestination(
      *xhc_dev, bsp_local_apic_id, pci::MSITriggerMode::kLevel,
      pci::MSIDeliveryMode::kFixed, InterruptVector::kXHCI, 0);

  const WithError<uint64_t> xhc_bar = pci::ReadBar(*xhc_dev, 0);
  printk("ReadBar: %s\n", xhc_bar.error.Name());
  const uint64_t xhc_mmio_base = xhc_bar.value & ~static_cast<uint64_t>(0xf);
  printk("xHC mmio_base = %08lx\n", xhc_mmio_base);

  usb::xhci::Controller xhc{xhc_mmio_base};

  if (0x8086 == pci::ReadVendorId(*xhc_dev))
  {
    SwitchEhci2Xhci(*xhc_dev);
  }
  {
    auto err = xhc.Initialize();
    printk("xhc.Initialize: %s\n", err.Name());
  }

  printk("xHC starting\n");
  xhc.Run();

  ::xhc = &xhc;

  usb::HIDMouseDriver::default_observer = MouseObserver;

  for (int i = 1; i <= xhc.MaxPorts(); ++i)
  {
    auto port = xhc.PortAt(i);
    printk("Port %d: IsConnected=%d\n", i, port.IsConnected());

    if (port.IsConnected())
    {
      if (auto err = ConfigurePort(xhc, port))
      {
        printk("Failed to configure port: %s at %s:%d\n", err.Name(),
               err.File(), err.Line());
        continue;
      }
    }
  }

  // const std::array available_memory_types{
  //     MemoryType::kEfiBootServicesCode,
  //     MemoryType::kEfiBootServicesData,
  //     MemoryType::kEfiConventionalMemory,
  // };

  // printk("Memory map: %p\n", &memory_map);
  // for (uintptr_t iter = reinterpret_cast<uintptr_t>(memory_map.buffer);
  //      iter < reinterpret_cast<uintptr_t>(memory_map.buffer) +
  //      memory_map.map_size; iter += memory_map.descriptor_size)
  // {
  //   auto desc = reinterpret_cast<MemoryDescriptor *>(iter);
  //   for (int i = 0; i < available_memory_types.size(); ++i)
  //   {
  //     if (desc->type == available_memory_types[i])
  //     {
  //       printk("type = %u, phys = %08lx, pages = %lu, attr = %08lx\n",
  //              desc->type,
  //              desc->physical_start,
  //              desc->physical_start + desc->number_of_pages * 4096 - 1,
  //              desc->number_of_pages,
  //              desc->attribute);
  //     }
  //   }
  // }

  // デスクトップ描画
  printk("Start desktop drawing...\n");

  const int kFrameWidth = GetFrameBufferConfig().horizontal_resolution;
  const int kFrameHeight = GetFrameBufferConfig().vertical_resolution;

  printk("Make shared window...\n");

  auto bgwindow = std::make_shared<Window>(kFrameWidth, kFrameHeight,
                                           GetFrameBufferConfig().pixel_format);
  auto bgwriter = bgwindow->Writer();

  printk("Draw desktop using bgwriter...\n");

  DrawDesktop(*bgwriter);
  // console->SetWindow(bgwindow);

  printk("Using bgwriter...\n");

  auto mouse_window = std::make_shared<Window>(
      kMouseCursorWidth, kMouseCursorHeight, GetFrameBufferConfig().pixel_format);
  mouse_window->SetTransparentColor(kMouseTransparentColor);

  printk("Draw mouse cursor...\n");

  DrawMouseCursor(mouse_window->Writer(), {0, 0});

  printk("Create frame buffer...\n");

  FrameBuffer screen;
  if (auto err = screen.Initialize(GetFrameBufferConfig()))
  {
    printk("Failed to initialize frame buffer: %s at %s:%d\n", err.Name(),
           err.File(), err.Line());
  }

  printk("Create window...\n");

  auto main_window = std::make_shared<Window>(160, 52, GetFrameBufferConfig().pixel_format);
  DrawWindow(*main_window->Writer(), "Hello Window");

  printk("Create layer manager...\n");

  layer_manager = new LayerManager;
  layer_manager->SetFrameBuffer(&screen);

  auto bglayer_id =
      layer_manager->NewLayer().SetWindow(bgwindow).Move({0, 0}).ID();
  mouse_layer_id =
      layer_manager->NewLayer().SetWindow(mouse_window).Move({200, 200}).ID();
  auto main_window_layer_id = layer_manager->NewLayer().SetWindow(main_window).SetDraggable(true).Move({300, 100}).ID();

  auto console_window = std::make_shared<Window>(
      Console::kColumns * 8, Console::kRows * 16, GetFrameBufferConfig().pixel_format);
  console->SetWindow(console_window);
  console->SetLayerID(layer_manager->NewLayer().SetWindow(console_window).Move({0, 0}).ID());

  layer_manager->UpDown(bglayer_id, 0);
  layer_manager->UpDown(console->LayerID(), 1);
  layer_manager->UpDown(mouse_layer_id, 2);
  layer_manager->UpDown(main_window_layer_id, 2);

  printk("Draw from layer manager... ");

  // layer_manager->Draw({{0, 0}, screen_size});

  printk("Drawing?");

  char str[128];
  unsigned int count = 0;

  // 割り込み処理
  std::array<Message, 32> main_queue_data;
  ArrayQueue<Message> main_queue{main_queue_data};
  ::main_queue = &main_queue;

  while (true)
  {
    ++count;
    sprintf(str, "%010u", count);
    FillRectangle(*main_window->Writer(), {24, 28}, {8 * 10, 16}, {0xc6, 0xc6, 0xc6});
    WriteString(*main_window->Writer(), {24, 28}, str, {0, 0, 0});
    layer_manager->Draw(main_window_layer_id);
    printk("Write...\n");

    __asm__("cli");
    if (main_queue.Count() == 0)
    {
      __asm__("sti\n\thlt");
      continue;
    }

    Message msg = main_queue.Front();
    main_queue.Pop();
    __asm__("sti");

    switch (msg.type)
    {
    case Message::kInterruptXHCI:
      while (xhc.PrimaryEventRing()->HasFront())
      {
        if (auto err = ProcessEvent(xhc))
        {
          printk("Error while ProcessEvent: %s at %s:%d\n", err.Name(),
                 err.File(), err.Line());
        }
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