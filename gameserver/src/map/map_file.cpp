#include "map/map_file.hpp"

#include <openssl/sha.h>

#include <format>
#include <fstream>
#include <ios>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs
{
namespace
{

constexpr std::string_view hex_digits = "0123456789abcdef";

} // namespace

std::expected<MapFile, std::string> MapFile::open(const std::filesystem::path& path)
{
    std::error_code error;

    const std::uintmax_t size = std::filesystem::file_size(path, error);

    if (error)
    {
        return std::unexpected(std::format(
            "Nie udało się otworzyć pliku terenu '{}': {}.",
            path.string(),
            error.message()));
    }

    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected(
            std::format("Nie udało się otworzyć pliku terenu '{}'.", path.string()));
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));

    if (!bytes.empty()
        && !file.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())))
    {
        return std::unexpected(std::format(
            "Plik terenu '{}' skończył się po {} bajtach z {}.",
            path.string(),
            file.gcount(),
            size));
    }

    std::expected<MapFile, std::string> loaded = from_bytes(std::move(bytes));

    if (!loaded)
    {
        // Ścieżka jest w komunikacie tylko tutaj: `from_bytes` nie wie, skąd wzięła bajty,
        // a bez nazwy pliku „nieznany kod terenu" nie mówi, którą mapę poprawić.
        return std::unexpected(std::format("Plik terenu '{}': {}", path.string(), loaded.error()));
    }

    return loaded;
}

std::expected<MapFile, std::string> MapFile::from_bytes(std::vector<std::uint8_t> bytes)
{
    MapFile file;
    file.bytes_ = std::move(bytes);

    std::expected<tmap::MapView, std::string> view = tmap::decode(file.bytes_);

    if (!view)
    {
        return std::unexpected(std::move(view.error()));
    }

    file.view_ = std::move(*view);

    SHA256(file.bytes_.data(), file.bytes_.size(), file.digest_.data());

    return file;
}

std::string MapFile::sha256_hex() const
{
    std::string hex;
    hex.reserve(digest_.size() * 2);

    for (const std::uint8_t byte : digest_)
    {
        hex.push_back(hex_digits[byte >> 4]);
        hex.push_back(hex_digits[byte & 0x0F]);
    }

    return hex;
}

} // namespace gs
