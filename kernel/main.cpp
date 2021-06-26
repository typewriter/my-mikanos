#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "asmfunc.h"
#include "frame_buffer_config.hpp"
#include "graphics.hpp"
#include "fonts.hpp"
#include "console.hpp"
#include "pci.hpp"
#include "mouse.hpp"
#include "error.hpp"
#include "interrupt.hpp"
#include "queue.hpp"
#include "usb/xhci/xhci.hpp"
#include "usb/classdriver/mouse.hpp"

const PixelColor kDesktopBGColor{58, 110, 165};
const PixelColor kDesktopFGColor{255, 255, 255};

// void *operator new(size_t size, void *buf)
// {
//   return buf;
// }

void operator delete(void *obj) noexcept
{
}

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

  console->PutString(s);
  return result;
}

void SwitchEhci2Xhci(const pci::Device &xhc_dev)
{
  bool intel_ehc_exist = false;
  for (int i = 0; i < pci::num_device; ++i)
  {
    if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x20u) /* EHCI */ && 0x8086 == pci::ReadVendorId(pci::devices[i]))
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
  printk("SwitchEhci2Xhci: SS = %02, xHCI = %02x\n", superspeed_ports, ehci2xhci_ports);
}

char mouse_cursor_buf[sizeof(MouseCursor)];
MouseCursor *mouse_cursor;

void MouseObserver(int8_t displacement_x, int8_t displacement_y)
{
  mouse_cursor->MoveRelative({displacement_x, displacement_y});
}

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

extern "C" void
KernelMain(const FrameBufferConfig &frame_buffer_config)
{
  switch (frame_buffer_config.pixel_format)
  {
  case kPixelRGBResv8BitPerColor:
    pixel_writer = new (pixel_writer_buf) RGBResv8BitPerColorPixelWriter{frame_buffer_config};
    break;
  case kPixelBGRResv8BitPerColor:
    pixel_writer = new (pixel_writer_buf) BGRResv8BitPerColorPixelWriter{frame_buffer_config};
    break;
  }

  // for (int x = 0; x < frame_buffer_config.horizontal_resolution; ++x)
  // {
  //   for (int y = 0; y < frame_buffer_config.vertical_resolution; ++y)
  //   {
  //     pixel_writer->Write(x, y, {255, 255, 255});
  //   }
  // }

  // for (int x = 0; x < 200; ++x)
  // {
  //   for (int y = 0; y < 100; ++y)
  //   {
  //     pixel_writer->Write(50 + x, 50 + y, {0, 255, 0});
  //   }
  // }

  // for (int x = 0; x < 100; ++x)
  // {
  //   for (int y = 0; y < 50; ++y)
  //   {
  //     pixel_writer->Write(350 + x, 200 + y, {0, 0, 255});
  //   }
  // }

  // int i = 0;
  // for (char c = '!'; c <= '~'; c++)
  // {
  //   WriteAscii(*pixel_writer, 50 + (i % 24 * 10), 50 + (i / 24 * 16), c, {0, 0, 0});
  //   i++;
  // }

  // WriteString(*pixel_writer, 50, 120, "Konnichiwa!!!", {0, 0, 128});

  // char buf[128];
  // sprintf(buf, "123 + 456 = %d, 12.3 + 34.5 = %.1f", 123 + 456, 12.3 + 34.5);
  // WriteString(*pixel_writer, 50, 140, buf, {0, 128, 0});

  // pixel_writer = new (pixel_writer_buf) RGBResv8BitPerColorPixelWriter{frame_buffer_config};
  // console = new (console_buf) Console{*pixel_writer, {0, 0, 0}, {255, 255, 255}};
  // for (int i = 0; i < 40; i++)
  // {
  //   sprintf(buf, "Line %d by PutString", i);
  //   console->PutString(buf);
  //   printk(", Line %d by printk\n", i);
  // }

  const int kFrameWidth = frame_buffer_config.horizontal_resolution;
  const int kFrameHeight = frame_buffer_config.vertical_resolution;

  FillRectangle(*pixel_writer,
                {0, 0},
                {kFrameWidth, kFrameHeight - 32},
                kDesktopBGColor);
  FillRectangle(*pixel_writer,
                {0, kFrameHeight - 32},
                {kFrameWidth, 32},
                {0, 0, 0});
  FillRectangle(*pixel_writer,
                {4, kFrameHeight - 28},
                {24, 24},
                {255, 255, 255});
  DrawRectangle(*pixel_writer,
                {kFrameWidth - 64, kFrameHeight - 28},
                {60, 24},
                {128, 128, 128});

  WriteString(*pixel_writer, kFrameWidth - 56, kFrameHeight - 24, "22:30", {255, 255, 255});

  console = new (console_buf) Console{*pixel_writer,
                                      kDesktopFGColor, kDesktopBGColor};
  printk("Konnichiwa!\n");

  mouse_cursor = new (mouse_cursor_buf) MouseCursor{
      pixel_writer, kDesktopBGColor, {300, 200}};

  auto err = pci::ScanAllBus();
  printk("ScanAllBus: %s\n", err.Name());

  for (int i = 0; i < pci::num_device; ++i)
  {
    const auto &dev = pci::devices[i];
    auto vendor_id = pci::ReadVendorId(dev.bus, dev.device, dev.function);
    auto class_code = pci::ReadClassCode(dev.bus, dev.device, dev.function);
    printk("%d.%d.%d: vend %04x, class %08x, head %02x\n",
           dev.bus, dev.device, dev.function, vendor_id, class_code, dev.header_type);
  }

  pci::Device *xhc_dev = nullptr;
  for (int i = 0; i < pci::num_device; ++i)
  {
    if (pci::devices[i].class_code.Match(0x0cu, 0x03u, 0x30u))
    {
      xhc_dev = &pci::devices[i];

      if (0x8086 == pci::ReadVendorId(xhc_dev->bus, xhc_dev->device, xhc_dev->function))
      {
        break;
      }
    }
  }

  if (xhc_dev)
  {
    printk("xHC has been found: %d.%d.%d\n", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
  }

  const uint16_t cs = GetCS();
  SetIDTEntry(idt[InterruptVector::kXHCI], MakeIDTAttr(DescriptorType::kInterruptGate, 0), reinterpret_cast<uint64_t>(IntHandlerXHCI), cs);
  LoadIDT(sizeof(idt) - 1, reinterpret_cast<uintptr_t>(&idt[0]));

  const uint8_t bsp_local_apic_id = *reinterpret_cast<const uint32_t *>(0xfee00020) >> 24;
  pci::ConfigureMSIFixedDestination(
      *xhc_dev, bsp_local_apic_id,
      pci::MSITriggerMode::kLevel, pci::MSIDeliveryMode::kFixed,
      InterruptVector::kXHCI, 0);

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
        printk("Failed to configure port: %s at %s:%d\n", err.Name(), err.File(), err.Line());
        continue;
      }
    }
  }

  std::array<Message, 32> main_queue_data;
  ArrayQueue<Message> main_queue{main_queue_data};
  ::main_queue = &main_queue;

  while (true)
  {
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
          printk("Error while ProcessEvent: %s at %s:%d\n", err.Name(), err.File(), err.Line());
        }
      }
      break;
    default:
      printk("Unknown message type: %d\n", msg.type);
    }
  }

  // while (1)
  // {
  //   if (auto err = ProcessEvent(xhc))
  //   {
  //     printk("Error while ProcessEvent: %s at %s:%d\n", err.Name(), err.File(), err.Line());
  //   }
  // }

  // for (int dy = 0; dy < kMouseCursorHeight; ++dy)
  // {
  //   for (int dx = 0; dx < kMouseCursorWidth; ++dx)
  //   {
  //     if (mouse_cursor_shape[dy][dx] == '@')
  //     {
  //       pixel_writer->Write(200 + dx, 100 + dy, {0, 0, 0});
  //     }
  //     else if (mouse_cursor_shape[dy][dx] == '.')
  //     {
  //       pixel_writer->Write(200 + dx, 100 + dy, {255, 255, 255});
  //     }
  //   }
  // }

  while (1)
    __asm__("hlt");
}

extern "C" void __cxa_pure_virtual()
{
  while (1)
    __asm__("hlt");
}