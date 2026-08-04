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

    // Flush po każdej linii, bo `std::cout` przekierowany na **potok** jest w pełni
    // buforowany — a orkiestrator zbiera wyjście procesu właśnie potokiem. Bez tego linie
    // INF wisiałyby w buforze do końca procesu i mecz, który się zawiesił, milczałby
    // dokładnie wtedy, gdy trzeba wiedzieć, na czym stanął. W konsoli różnicy nie widać,
    // bo tam bufor i tak kończy się na nowej linii — i dlatego usterki nie widać w dev.
    // Koszt jest żaden: linii jest kilka na start i jedna na pięć sekund.
    stream << std::format("{:%FT%T}Z {} {}\n", now, label(level), message) << std::flush;
}

} // namespace gs::log
