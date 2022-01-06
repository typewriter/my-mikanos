#include "window.hpp"
#include "graphics.hpp"
#include "fonts.hpp"
#include "logger.hpp"
#include "layer.hpp"

namespace
{
    const int kCloseButtonWidth = 16;
    const int kCloseButtonHeight = 14;
    const char close_button[kCloseButtonHeight][kCloseButtonWidth + 1] = {
        "...............@",
        ".:::::::::::::$@",
        ".:::::::::::::$@",
        ".:::@@::::@@::$@",
        ".::::@@::@@:::$@",
        ".:::::@@@@::::$@",
        ".::::::@@:::::$@",
        ".:::::@@@@::::$@",
        ".::::@@::@@:::$@",
        ".:::@@::::@@::$@",
        ".:::::::::::::$@",
        ".:::::::::::::$@",
        ".$$$$$$$$$$$$$$@",
        "@@@@@@@@@@@@@@@@",
    };
}

void DrawWindow(PixelWriter &writer, const char *title)
{
    auto fill_rect = [&writer](Vector2D<int> pos, Vector2D<int> size, uint32_t c)
    {
        FillRectangle(writer, pos, size, ToColor(c));
    };
    const auto win_w = writer.Width();
    const auto win_h = writer.Height();

    fill_rect({0, 0}, {win_w, 1}, 0xc6c6c6);
    fill_rect({1, 1}, {win_w - 2, 1}, 0xffffff);
    fill_rect({0, 0}, {1, win_h}, 0xc6c6c6);
    fill_rect({1, 1}, {1, win_h - 2}, 0xffffff);
    fill_rect({win_w - 2, 1}, {1, win_h - 2}, 0x848484);
    fill_rect({win_w - 1, 0}, {1, win_h}, 0x000000);
    fill_rect({2, 2}, {win_w - 4, win_h - 4}, 0xc6c6c6);
    fill_rect({1, win_h - 2}, {win_w - 2, 1}, 0x848484);
    fill_rect({0, win_h - 1}, {win_w, 1}, 0x000000);

    DrawWindowTitle(writer, title, false);
}

Window::Window(int width, int height, PixelFormat shadow_format) : width_{width}, height_{height}
{
    data_.resize(height);
    for (int y = 0; y < height; ++y)
    {
        data_[y].resize(width);
    }

    FrameBufferConfig config{};
    config.frame_buffer = nullptr;
    config.horizontal_resolution = width;
    config.vertical_resolution = height;
    config.pixel_format = shadow_format;

    if (auto err = shadow_buffer_.Initialize(config))
    {
        Log(kError, "Failed to initialize shadow buffer: %s at %s:%d\n", err.Name(), err.File(), err.Line());
    }
}

void Window::DrawTo(FrameBuffer &dst, Vector2D<int> position, const Rectangle<int> &area)
{
    if (!transparent_color_)
    {
        Rectangle<int> window_area{position, Size()};
        Rectangle<int> intersection = area & window_area;
        dst.Copy(intersection.pos, shadow_buffer_, {intersection.pos - position, intersection.size});
        return;
    }

    const auto tc = transparent_color_.value();
    auto &writer = dst.Writer();
    for (int y = std::max(0, 0 - position.y); y < std::min(Height(), writer.Height() - position.y); ++y)
    {
        for (int x = std::max(0, 0 - position.x); x < std::min(Width(), writer.Width() - position.x); ++x)
        {
            const auto c = At(x, y);
            if (c != tc)
            {
                writer.Write({position.x + x, position.y + y}, c);
            }
        }
    }
}

void Window::SetTransparentColor(std::optional<PixelColor> c)
{
    transparent_color_ = c;
}

Window::WindowWriter *Window::Writer()
{
    return &writer_;
}

PixelColor &Window::At(int x, int y)
{
    return data_[y][x];
}

const PixelColor &Window::At(int x, int y) const
{
    return data_[y][x];
}

void Window::Write(Vector2D<int> pos, PixelColor c)
{
    data_[pos.y][pos.x] = c;
    shadow_buffer_.Writer().Write({pos.x, pos.y}, c);
}

void Window::Move(Vector2D<int> dst_pos, const Rectangle<int> &src)
{
    shadow_buffer_.Move(dst_pos, src);
}

Vector2D<int> Window::Size() const
{
    return {width_, height_};
}

int Window::Width() const
{
    return width_;
}

int Window::Height() const
{
    return height_;
}

ToplevelWindow::ToplevelWindow(int width, int height, PixelFormat shadow_format, const std::string& title)
    : Window{width, height, shadow_format}, title_{title}
{
    DrawWindow(*Writer(), title_.c_str());
}

void ToplevelWindow::Activate()
{
    Window::Activate();
    DrawWindowTitle(*Writer(), title_.c_str(), true);
}

void ToplevelWindow::Deactivate()
{
    Window::Deactivate();
    DrawWindowTitle(*Writer(), title_.c_str(), false);
}

Vector2D<int> ToplevelWindow::InnerSize() const {
    return Size() - kTopLeftMargin - kBottomRightMargin;
}

void DrawTextbox(
    PixelWriter& writer,
    Vector2D<int> pos,
    Vector2D<int> size,
    PixelColor bgColor,
    PixelColor edge1Color,
    PixelColor edge2Color
    )
{
    auto fill_rect = [&writer](Vector2D<int> pos, Vector2D<int> size, PixelColor c) {
        FillRectangle(writer, pos, size, c);
    };

    fill_rect(pos + Vector2D<int>{1, 1}, size - Vector2D<int>{2, 2}, bgColor);
    fill_rect(pos, {size.x, 1}, edge2Color);
    fill_rect(pos, {1, size.y}, edge2Color);
    fill_rect(pos + Vector2D<int>{0, size.y}, {size.x, 1}, edge1Color);
    fill_rect(pos + Vector2D<int>{size.x, 0}, {1, size.y}, edge1Color);
}

void DrawTextbox(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size) {
    DrawTextbox(writer, pos, size, ToColor(0xffffff), ToColor(0xc6c6c6), ToColor(0x848484));
}

std::shared_ptr<ToplevelWindow> main_window;
unsigned int main_window_layer_id;

void InitializeMainWindow() {
  main_window = std::make_shared<ToplevelWindow>(160, 52, GetFrameBufferConfig().pixel_format, "Hello window");
//   DrawWindow(*main_window->Writer(), "Hello Window");

  main_window_layer_id = layer_manager->NewLayer().SetWindow(main_window).SetDraggable(true).Move({300, 100}).ID();
  layer_manager->UpDown(main_window_layer_id, 2);
}

void DrawWindowTitle(PixelWriter& writer, const char* title, bool active)
{
    const auto win_w = writer.Width();
    uint32_t bgcolor = 0x848484;
    if (active)
    {
        bgcolor = 0x000084;
    }

    FillRectangle(writer, {3, 3}, {win_w - 6, 18}, ToColor(bgcolor));
    WriteString(writer, {24, 4}, title, ToColor(0xffffff));

    for (int y = 0; y < kCloseButtonHeight; ++y)
    {
        for (int x = 0; x < kCloseButtonWidth; ++x)
        {
            PixelColor c = ToColor(0xffffff);
            if (close_button[y][x] == '@')
            {
                c = ToColor(0x000000);
            } else if (close_button[y][x] == '$')
            {
                c = ToColor(0x848484);
            } else if (close_button[y][x] == ':')
            {
                c = ToColor(0xc6c6c6);
            }
            writer.Write({win_w - 5 - kCloseButtonWidth + x, 5 + y}, c);
        }
    }
}

void DrawTerminal(PixelWriter& writer, Vector2D<int> pos, Vector2D<int> size) {
    DrawTextbox(writer, pos, size, ToColor(0x000000), ToColor(0xc6c6c6), ToColor(0x848484));
}