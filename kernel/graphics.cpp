#include "graphics.hpp"
#include "fonts.hpp"
#include "console.hpp"

void RGBResv8BitPerColorPixelWriter::Write(int x, int y, const PixelColor &c)
{
    auto p = PixelAt(x, y);
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
}

void BGRResv8BitPerColorPixelWriter::Write(int x, int y, const PixelColor &c)
{
    auto p = PixelAt(x, y);
    p[0] = c.b;
    p[1] = c.g;
    p[2] = c.r;
}

void FillRectangle(PixelWriter &writer, const Vector2D<int> &pos,
                   const Vector2D<int> &size, const PixelColor &c)
{
    for (int dy = 0; dy < size.y; ++dy)
    {
        for (int dx = 0; dx < size.x; ++dx)
        {
            writer.Write(pos.x + dx, pos.y + dy, c);
        }
    }
}

void DrawRectangle(PixelWriter &writer, const Vector2D<int> &pos,
                   const Vector2D<int> &size, const PixelColor &c)
{
    for (int dx = 0; dx < size.x; ++dx)
    {
        writer.Write(pos.x + dx, pos.y, c);
        writer.Write(pos.x + dx, pos.y + size.y - 1, c);
    }
    for (int dy = 1; dy < size.y - 1; ++dy)
    {
        writer.Write(pos.x, pos.y + dy, c);
        writer.Write(pos.x + size.x - 1, pos.y + dy, c);
    }
}

void DrawDesktop(PixelWriter &writer)
{
    const auto width = writer.Width();
    const auto height = writer.Height();

    FillRectangle(writer,
                  {0, 0},
                  {width, height - 32},
                  kDesktopBGColor);
    FillRectangle(writer,
                  {0, height - 32},
                  {width, 32},
                  {0, 0, 0});
    FillRectangle(writer,
                  {4, height - 28},
                  {24, 24},
                  {255, 255, 255});
    DrawRectangle(writer,
                  {width - 64, height - 28},
                  {60, 24},
                  {128, 128, 128});

    WriteString(writer, width - 56, height - 24, "22:30", {255, 255, 255});
}