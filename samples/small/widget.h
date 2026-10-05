#pragma once

#include <cstdint>
#include <memory>

namespace demo {

enum class Align : std::uint8_t { Left, Center, Right };

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int px, int py) const
    {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

class Widget {
public:
    virtual ~Widget() = default;
    virtual void paint() {}

    void setGeometry(const Rect &rect) { m_rect = rect; }
    const Rect &geometry() const { return m_rect; }

protected:
    Rect m_rect;
    Align m_align = Align::Left;
    std::unique_ptr<Widget> m_child;
};

} // namespace demo
