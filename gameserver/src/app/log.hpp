#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace gs::log
{

enum class Level
{
    info,
    warn,
    error
};

/// Jedyne miejsce, które faktycznie pisze. Reszta to cukier na `std::format`.
///
/// Bez biblioteki logującej: proces żyje kilkanaście minut, pisze kilkadziesiąt linii
/// i jego wyjście czyta orkiestrator (§9). Cokolwiek więcej byłoby zależnością utrzymywaną
/// dla samego siebie.
void write(Level level, std::string_view message);

template <typename... Args>
void info(std::format_string<Args...> format, Args&&... args)
{
    write(Level::info, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void warn(std::format_string<Args...> format, Args&&... args)
{
    write(Level::warn, std::format(format, std::forward<Args>(args)...));
}

template <typename... Args>
void error(std::format_string<Args...> format, Args&&... args)
{
    write(Level::error, std::format(format, std::forward<Args>(args)...));
}

} // namespace gs::log
