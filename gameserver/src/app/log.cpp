#include "app/log.hpp"

#include <chrono>
#include <iostream>

namespace gs::log
{
namespace
{

std::string_view label(Level level)
{
    switch (level)
    {
    case Level::info:
        return "INF";
    case Level::warn:
        return "WRN";
    case Level::error:
        return "ERR";
    }

    return "???";
}

} // namespace

void write(Level level, std::string_view message)
{
    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());

    // Ostrzeżenia i błędy na stderr, żeby dały się oddzielić bez parsowania treści —
    // orkiestrator i tak zbiera oba strumienie, ale nie musi ich rozumieć.
    std::ostream& stream = level == Level::info ? std::cout : std::cerr;

    stream << std::format("{:%FT%T}Z {} {}\n", now, label(level), message);
}

} // namespace gs::log
