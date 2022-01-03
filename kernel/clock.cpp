#include "clock.hpp"
#include "fonts.hpp"
#include "asmfunc.h"
#include <memory>
#include "layer.hpp"
#include "window.hpp"
#include "frame_buffer.hpp"

std::shared_ptr<Window> clock_window;
unsigned int clock_layer_id;
const int clock_width = 80;

void InitializeClock() {
  auto frame_buffer_config = GetFrameBufferConfig();
  // 時計レイヤーを生成
  clock_window = std::make_shared<Window>(clock_width, 32, frame_buffer_config.pixel_format);
  auto clock_writer = clock_window->Writer();

  // 時計レイヤーを上書き
  clock_layer_id = layer_manager->NewLayer().SetWindow(clock_window).Move({static_cast<int>(frame_buffer_config.horizontal_resolution) - clock_width, static_cast<int>(frame_buffer_config.vertical_resolution) - 32}).ID();
  layer_manager->UpDown(clock_layer_id, 1);

  DrawClock(*clock_writer);
}

void DrawClock(PixelWriter& writer) {
  // RTC からクロック読み込み（ふつうは一度だけ読み込んで、その後は自前で計算する）
  __asm__("cli");
  IoOutb(0x70, 0x00);
  uint8_t second = IoInb(0x71);
  IoOutb(0x70, 0x02);
  uint8_t minute = IoInb(0x71);
  IoOutb(0x70, 0x04);
  uint8_t hour = IoInb(0x71);
  IoOutb(0x70, 0x0B);
  uint8_t statusB = IoInb(0x71);
  __asm__("sti");

  // BCD モードの場合
  if (!(statusB & 0x04))
  {
    second = (second & 0x0F) + ((second / 16) * 10);
    minute = (minute & 0x0F) + ((minute / 16) * 10);
    hour = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
  }

  // 12 時間モードの場合
  if (!(statusB & 0x02) && (hour & 0x80))
  {
    hour = ((hour & 0x7F) + 12) % 24;
  }

  // UTC -> JST
  hour = (hour + 9) % 24;

  char str[128];
  sprintf(str, "%02d:%02d:%02d", hour, minute, second);
  FillRectangle(*clock_window->Writer(), {4, 4}, {clock_width - 8, 24}, {0x00, 0x00, 0x00});
  DrawRectangle(*clock_window->Writer(), {4, 4}, {clock_width - 8, 24}, {0xFF, 0xFF, 0xFF});
  WriteString(*clock_window->Writer(), {8, 8}, str, {0xFF, 0xFF, 0xFF});
  layer_manager->Draw(clock_layer_id);
}