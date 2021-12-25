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

char pixel_writer_buf[sizeof(RGBResv8BitPerColorPixelWriter)];
PixelWriter *pixel_writer;

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
Vector2D<int> screen_size;
Vector2D<int> mouse_position;

template <typename T>
Vector2D<T> ElementMin(Vector2D<T> a, Vector2D<T> b)
{
  return Vector2D<T>{std::min(a.x, b.x), std::min(a.y, b.y)};
}

template <typename T>
Vector2D<T> ElementMax(Vector2D<T> a, Vector2D<T> b)
{
  return Vector2D<T>{std::max(a.x, b.x), std::max(a.y, b.y)};
}

void MouseObserver(int8_t displacement_x, int8_t displacement_y)
{
  auto newpos = mouse_position + Vector2D<int>{displacement_x, displacement_y};
  newpos = ElementMin(newpos, screen_size + Vector2D<int>{-1, -1});
  mouse_position = ElementMax(newpos, {0, 0});

  layer_manager->Move(mouse_layer_id, mouse_position);
  layer_manager->Draw();

  // layer_manager->MoveRelative(mouse_layer_id, {displacement_x, displacement_y});
  // StartLAPICTimer();
  // layer_manager->Draw();
  // auto elapsed = LAPICTimerElapsed();
  // StopLAPICTimer();
  // printk("MouseObserver: elapsed = %u\n", elapsed);
  // mouse_cursor->MoveRelative({displacement_x, displacement_y});
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
  // コンソール表示
  FrameBufferConfig frame_buffer_config{frame_buffer_config_ref};
  MemoryMap memory_map{memory_map_ref};
  switch (frame_buffer_config.pixel_format)
  {
  case kPixelRGBResv8BitPerColor:
    pixel_writer = new (pixel_writer_buf)
        RGBResv8BitPerColorPixelWriter{frame_buffer_config};
    break;
  case kPixelBGRResv8BitPerColor:
    pixel_writer = new (pixel_writer_buf)
        BGRResv8BitPerColorPixelWriter{frame_buffer_config};
    break;
  }

  screen_size.x = frame_buffer_config.horizontal_resolution;
  screen_size.y = frame_buffer_config.vertical_resolution;

  DrawDesktop(*pixel_writer);

  console = new (console_buf) Console{kDesktopFGColor, kDesktopBGColor};
  console->SetWriter(pixel_writer);

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

  const int kFrameWidth = frame_buffer_config.horizontal_resolution;
  const int kFrameHeight = frame_buffer_config.vertical_resolution;

  printk("Make shared window...\n");

  auto bgwindow = std::make_shared<Window>(kFrameWidth, kFrameHeight,
                                           frame_buffer_config.pixel_format);
  auto bgwriter = bgwindow->Writer();

  printk("Draw desktop using bgwriter...\n");

  DrawDesktop(*bgwriter);
  console->SetWindow(bgwindow);

  printk("Using bgwriter...\n");

  auto mouse_window = std::make_shared<Window>(
      kMouseCursorWidth, kMouseCursorHeight, frame_buffer_config.pixel_format);
  mouse_window->SetTransparentColor(kMouseTransparentColor);

  printk("Draw mouse cursor...\n");

  DrawMouseCursor(mouse_window->Writer(), {0, 0});

  printk("Create frame buffer...\n");

  FrameBuffer screen;
  if (auto err = screen.Initialize(frame_buffer_config))
  {
    printk("Failed to initialize frame buffer: %s at %s:%d\n", err.Name(),
           err.File(), err.Line());
  }

  printk("Create window...\n");

  auto main_window = std::make_shared<Window>(160, 52, frame_buffer_config.pixel_format);
  DrawWindow(*main_window->Writer(), "Hello Window");

  printk("Create layer manager...\n");

  layer_manager = new LayerManager;
  layer_manager->SetFrameBuffer(&screen);

  auto bglayer_id =
      layer_manager->NewLayer().SetWindow(bgwindow).Move({0, 0}).ID();
  mouse_layer_id =
      layer_manager->NewLayer().SetWindow(mouse_window).Move({200, 200}).ID();
  auto main_window_layer_id = layer_manager->NewLayer().SetWindow(main_window).Move({300, 100}).ID();

  layer_manager->UpDown(bglayer_id, 0);
  layer_manager->UpDown(mouse_layer_id, 1);
  layer_manager->UpDown(main_window_layer_id, 1);

  printk("Draw from layer manager...");

  layer_manager->Draw();

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
    layer_manager->Draw();

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