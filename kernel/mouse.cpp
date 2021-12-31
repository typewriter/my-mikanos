#include "mouse.hpp"
#include "graphics.hpp"
#include "layer.hpp"
#include "console.hpp"
// #include "usb/device.hpp"
// #include "usb/classdriver/hid.hpp"
#include "usb/classdriver/mouse.hpp"

namespace
{
    const char mouse_cursor_shape[kMouseCursorHeight][kMouseCursorWidth + 1] = {
        "@              ",
        "@@             ",
        "@.@            ",
        "@..@           ",
        "@...@          ",
        "@....@         ",
        "@.....@        ",
        "@......@       ",
        "@.......@      ",
        "@........@     ",
        "@.........@    ",
        "@..........@   ",
        "@...........@  ",
        "@............@ ",
        "@......@@@@@@@@",
        "@......@       ",
        "@....@@.@      ",
        "@...@ @.@      ",
        "@..@   @.@     ",
        "@.@    @.@     ",
        "@@      @.@    ",
        "@       @.@    ",
        "         @.@   ",
        "         @@@   ",
    };
}

void DrawMouseCursor(PixelWriter *pixel_writer, Vector2D<int> position)
{
    for (int dy = 0; dy < kMouseCursorHeight; ++dy)
    {
        for (int dx = 0; dx < kMouseCursorWidth; ++dx)
        {
            if (mouse_cursor_shape[dy][dx] == '@')
            {
                pixel_writer->Write({position.x + dx, position.y + dy}, {0, 0, 0});
            }
            else if (mouse_cursor_shape[dy][dx] == '.')
            {
                pixel_writer->Write({position.x + dx, position.y + dy}, {255, 255, 255});
            }
            else
            {
                pixel_writer->Write({position.x + dx, position.y + dy}, kMouseTransparentColor);
            }
        }
    }
}

Vector2D<int> mouse_position;
unsigned int mouse_layer_id;

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

void InitializeMouse() {
    auto mouse_window = std::make_shared<Window>(
        kMouseCursorWidth, kMouseCursorHeight, GetFrameBufferConfig().pixel_format);
    mouse_window->SetTransparentColor(kMouseTransparentColor);
    mouse_layer_id =
        layer_manager->NewLayer().SetWindow(mouse_window).Move({200, 200}).ID();

    printk("Draw mouse cursor...\n");

    DrawMouseCursor(mouse_window->Writer(), {0, 0});
    layer_manager->UpDown(mouse_layer_id, 3);

    usb::HIDMouseDriver::default_observer = MouseObserver;
}