#include <cstdint>
#include <cstddef>
#include <cstdio>
#include "frame_buffer_config.hpp"
#include "graphics.hpp"
#include "fonts.hpp"
#include "console.hpp"

void *
operator new(size_t size, void *buf)
{
  return buf;
}

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

  for (int x = 0; x < frame_buffer_config.horizontal_resolution; ++x)
  {
    for (int y = 0; y < frame_buffer_config.vertical_resolution; ++y)
    {
      pixel_writer->Write(x, y, {255, 255, 255});
    }
  }

  for (int x = 0; x < 200; ++x)
  {
    for (int y = 0; y < 100; ++y)
    {
      pixel_writer->Write(50 + x, 50 + y, {0, 255, 0});
    }
  }

  for (int x = 0; x < 100; ++x)
  {
    for (int y = 0; y < 50; ++y)
    {
      pixel_writer->Write(350 + x, 200 + y, {0, 0, 255});
    }
  }

  int i = 0;
  for (char c = '!'; c <= '~'; c++)
  {
    WriteAscii(*pixel_writer, 50 + (i % 24 * 10), 50 + (i / 24 * 16), c, {0, 0, 0});
    i++;
  }

  WriteString(*pixel_writer, 50, 120, "Konnichiwa!!!", {0, 0, 128});

  char buf[128];
  sprintf(buf, "123 + 456 = %d, 12.3 + 34.5 = %.1f", 123 + 456, 12.3 + 34.5);
  WriteString(*pixel_writer, 50, 140, buf, {0, 128, 0});

  pixel_writer = new (pixel_writer_buf) RGBResv8BitPerColorPixelWriter{frame_buffer_config};
  console = new (console_buf) Console{*pixel_writer, {0, 0, 0}, {255, 255, 255}};
  for (int i = 0; i < 40; i++)
  {
    sprintf(buf, "Line %d by PutString", i);
    console->PutString(buf);
    printk(", Line %d by printk\n", i);
  }

  while (1)
    __asm__("hlt");
}
