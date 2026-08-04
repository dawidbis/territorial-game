#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

/// Czytnik PNG, celowo najwęższy, jaki da się napisać.
///
/// Przyjmuje **wyłącznie 8-bitowy RGB albo RGBA bez przeplotu** — czyli to, co wychodzi
/// z każdego edytora graficznego po zwykłym „zapisz jako PNG". Wszystko inne jest błędem
/// z nazwą tego, co nie pasuje, a nie próbą konwersji.
///
/// Ta surowość jest tą samą zasadą, co „nieznany kolor piksela to błąd": mapa jest siatką
/// danych udającą obrazek, więc cicha konwersja 16 bitów na 8 albo rozwinięcie palety
/// zamieniłaby cudzy zamiar w nasze zgadywanie. Biblioteka ogólnego przeznaczenia zrobiłaby
/// dokładnie to, czego tu nie chcemy.
namespace gs::png
{

struct Image
{
    std::uint32_t width = 0;

    std::uint32_t height = 0;

    /// Trzy bajty na piksel, wiersz po wierszu. Kanał alfa jest odrzucany przy dekodowaniu:
    /// siatka terenu nie ma jak być półprzezroczysta.
    std::vector<std::uint8_t> rgb;
};

std::expected<Image, std::string> decode(std::span<const std::uint8_t> bytes);

std::expected<Image, std::string> read_file(const std::filesystem::path& path);

} // namespace gs::png
