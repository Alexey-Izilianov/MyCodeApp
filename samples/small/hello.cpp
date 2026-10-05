// Малый C++: препроцессор, шаблоны, лямбды, литералы всех видов
#include <algorithm>
#include <string>
#include <vector>
#include "widget.h"

#define MAX_ITEMS 128
#define SQUARE(x) ((x) * (x))

#ifdef _WIN32
#pragma once
#endif

namespace demo {

/* Многострочный комментарий:
   подсветка должна держаться
   на всех трёх строках */
template <typename T>
constexpr T clampValue(T value, T low, T high)
{
    return std::max(low, std::min(value, high));
}

class Counter final : public Widget {
public:
    explicit Counter(std::string name) : m_name(std::move(name)) {}
    ~Counter() override = default;

    void increment() noexcept { ++m_value; }
    [[nodiscard]] int value() const { return m_value; }

private:
    std::string m_name;
    int m_value = 0;
    static inline const char *kLabel = "counter";
};

int run()
{
    std::vector<int> numbers{1, 2, 3, 0x1F, 0b1010, 42u};
    const double ratio = 3.14e-2f;
    const char quote = '\'';
    const char *escaped = "tab\there, newline\n, quote \" done";
    const auto raw = R"(raw string with "quotes" and \n not escaped)";

    auto sum = 0;
    std::for_each(numbers.begin(), numbers.end(), [&sum](int n) { sum += SQUARE(n); });

    Counter counter("clicks");
    for (int i = 0; i < MAX_ITEMS; ++i) {
        if (i % 2 == 0 && ratio > 0.0)
            counter.increment();
        else if (quote == 'x')
            break;
    }
    switch (counter.value()) {
    case 0: return -1;
    default: break;
    }
    return clampValue(sum, 0, 1000) + static_cast<int>(sizeof(escaped)) + (raw != nullptr);
}

} // namespace demo
