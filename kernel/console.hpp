#pragma once

#include "window.hpp"
#include "graphics.hpp"

class Console
{
public:
    static const int kRows = 25, kColumns = 80;
    Console(const PixelColor &fg_color, const PixelColor &bg_color);
    void PutString(const char *s);
    void Println(const char *s);
    void SetWriter(PixelWriter *writer);
    void SetWindow(const std::shared_ptr<Window> &window);
    void SetLayerID(unsigned int layer_id);
    unsigned int LayerID() const;
    void Refresh();

private:
    void Newline();

    PixelWriter *writer_;
    std::shared_ptr<Window> window_;
    const PixelColor fg_color_, bg_color_;
    char buffer_[kRows][kColumns + 1];
    int cursor_row_, cursor_column_;
    int layer_id_;
};

void InitializeConsole();
int printk(const char *format, ...);
Console &GetConsole();