#pragma once
#include <algorithm>

#include "frame_buffer_config.hpp"

template <typename T>
struct Vector2D
{
    T x, y;

    template <typename U>
    Vector2D<T> &operator+=(const Vector2D<U> &rhs)
    {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
};

template <typename T, typename U>
auto operator+(const Vector2D<T> &lhs, const Vector2D<U> &rhs)
    -> Vector2D<decltype(lhs.x + rhs.x)>
{
    return {lhs.x + rhs.x, lhs.y + rhs.y};
}

template <typename T, typename U>
auto operator-(const Vector2D<T> &lhs, const Vector2D<U> &rhs)
    -> Vector2D<decltype(lhs.x + rhs.x)>
{
    return {lhs.x - rhs.x, lhs.y - rhs.y};
}

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

template <typename T>
struct Rectangle
{
    Vector2D<T> pos, size;
};

template <typename T, typename U>
Rectangle<T> operator&(const Rectangle<T> &lhs, const Rectangle<U> &rhs)
{
    const auto lhs_end = lhs.pos + lhs.size;
    const auto rhs_end = rhs.pos + rhs.size;
    if (lhs_end.x < rhs.pos.x || lhs_end.y < rhs.pos.y ||
        rhs_end.x < lhs.pos.x || rhs_end.y < lhs.pos.y)
    {
        return {{0, 0}, {0, 0}};
    }

    auto new_pos = ElementMax(lhs.pos, rhs.pos);
    auto new_size = ElementMin(lhs_end, rhs_end) - new_pos;
    return {new_pos, new_size};
}

struct PixelColor
{
    uint8_t r, g, b;
};

inline bool operator==(const PixelColor &lhs, const PixelColor &rhs)
{
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

inline bool operator!=(const PixelColor &lhs, const PixelColor &rhs)
{
    return !(lhs == rhs);
}

const PixelColor kDesktopBGColor{58, 110, 165};
const PixelColor kDesktopFGColor{255, 255, 255};

class PixelWriter
{
public:
    virtual ~PixelWriter() = default;
    virtual void Write(Vector2D<int> pos, const PixelColor &c) = 0;
    virtual int Width() const = 0;
    virtual int Height() const = 0;
};

class FrameBufferWriter : public PixelWriter
{
public:
    FrameBufferWriter(const FrameBufferConfig &config) : config_{config} {}
    virtual ~FrameBufferWriter() = default;
    virtual int Width() const override { return config_.horizontal_resolution; }
    virtual int Height() const override { return config_.vertical_resolution; }

protected:
    uint8_t *PixelAt(int x, int y)
    {
        return config_.frame_buffer + 4 * (config_.pixels_per_scan_line * y + x);
    }

private:
    const FrameBufferConfig &config_;
};

class RGBResv8BitPerColorPixelWriter : public FrameBufferWriter
{
public:
    using FrameBufferWriter::FrameBufferWriter;
    virtual void Write(Vector2D<int> pos, const PixelColor &c) override;
};

class BGRResv8BitPerColorPixelWriter : public FrameBufferWriter
{
public:
    using FrameBufferWriter::FrameBufferWriter;
    virtual void Write(Vector2D<int> pos, const PixelColor &c) override;
};

void FillRectangle(PixelWriter &writer, const Vector2D<int> &pos,
                   const Vector2D<int> &size, const PixelColor &c);
void DrawRectangle(PixelWriter &writer, const Vector2D<int> &pos,
                   const Vector2D<int> &size, const PixelColor &c);
void DrawDesktop(PixelWriter &writer);

void InitializeGraphics(const FrameBufferConfig &frame_buffer_config_ref);
FrameBufferConfig GetFrameBufferConfig();
PixelWriter *GetPixelWriter();

Vector2D<int> ScreenSize();

constexpr PixelColor ToColor(uint32_t c)
{
    return {
        static_cast<uint8_t>((c >> 16) & 0xff),
        static_cast<uint8_t>((c >> 8) & 0xff),
        static_cast<uint8_t>(c & 0xff)};
}

extern PixelWriter *pixel_writer;