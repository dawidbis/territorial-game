#include "png.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <fstream>
#include <ios>
#include <string_view>
#include <system_error>

namespace gs::png
{
namespace
{

constexpr std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

/// PNG jest big-endian, w przeciwieństwie do `.tmap`. Warto to widzieć w nazwie, bo obie
/// funkcje stoją w sąsiednich plikach i różnią się wyłącznie kolejnością.
std::uint32_t read_u32_be(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24)
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 8)
        | static_cast<std::uint32_t>(bytes[offset + 3]);
}

/// Predyktor z §9.4 specyfikacji PNG: wybiera tego z trzech sąsiadów, do którego najbliżej
/// ma ich prosta suma.
std::uint8_t paeth(std::uint8_t left, std::uint8_t above, std::uint8_t corner)
{
    const int estimate = static_cast<int>(left) + static_cast<int>(above) - static_cast<int>(corner);

    const int to_left = std::abs(estimate - static_cast<int>(left));
    const int to_above = std::abs(estimate - static_cast<int>(above));
    const int to_corner = std::abs(estimate - static_cast<int>(corner));

    if (to_left <= to_above && to_left <= to_corner)
    {
        return left;
    }

    return to_above <= to_corner ? above : corner;
}

struct Header
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bit_depth = 0;
    std::uint8_t colour_type = 0;
    std::uint8_t interlace = 0;
};

std::expected<Header, std::string> parse_header(std::span<const std::uint8_t> data)
{
    if (data.size() != 13)
    {
        return std::unexpected(std::format("Nagłówek IHDR ma {} bajtów zamiast 13.", data.size()));
    }

    Header header;
    header.width = read_u32_be(data, 0);
    header.height = read_u32_be(data, 4);
    header.bit_depth = data[8];
    header.colour_type = data[9];
    header.interlace = data[12];

    if (header.width == 0 || header.height == 0)
    {
        return std::unexpected(
            std::format("Obrazek ma wymiary {}×{}.", header.width, header.height));
    }

    if (header.bit_depth != 8)
    {
        return std::unexpected(std::format(
            "Obrazek ma {} bitów na kanał, a czytnik przyjmuje wyłącznie 8. Zapisz mapę jako "
            "8-bitowy PNG.",
            header.bit_depth));
    }

    if (header.colour_type != 2 && header.colour_type != 6)
    {
        return std::unexpected(std::format(
            "Obrazek jest w trybie koloru {}, a czytnik przyjmuje 2 (RGB) i 6 (RGBA). Tryb "
            "z paletą albo w odcieniach szarości trzeba przekonwertować na RGB.",
            header.colour_type));
    }

    // Kompresja i filtrowanie mają w PNG dokładnie po jednej dozwolonej wartości, więc
    // sprawdzanie ich byłoby sprawdzaniem, czy plik jest w ogóle PNG-iem. Przeplot to co
    // innego: jest legalny, a rozwijanie siedmiu przebiegów Adama7 to drugie tyle kodu dla
    // czegoś, czego mapa nigdy nie potrzebuje.
    if (header.interlace != 0)
    {
        return std::unexpected(std::string{
            "Obrazek jest zapisany z przeplotem (Adam7). Zapisz go bez przeplotu."});
    }

    return header;
}

std::expected<std::vector<std::uint8_t>, std::string> inflate_all(
    std::span<const std::uint8_t> compressed,
    std::size_t expected_size)
{
    std::vector<std::uint8_t> raw(expected_size);

    z_stream stream{};

    if (inflateInit(&stream) != Z_OK)
    {
        return std::unexpected(std::string{"Nie udało się zainicjować dekompresji zlib."});
    }

    stream.next_in = const_cast<Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = raw.data();
    stream.avail_out = static_cast<uInt>(raw.size());

    const int result = inflate(&stream, Z_FINISH);
    const uInt left = stream.avail_out;

    inflateEnd(&stream);

    if (result != Z_STREAM_END)
    {
        return std::unexpected(
            std::format("Strumień IDAT jest uszkodzony (zlib zwrócił {}).", result));
    }

    if (left != 0)
    {
        return std::unexpected(std::format(
            "Rozpakowane dane mają {} bajtów, a wymiary z IHDR zapowiadają {}.",
            raw.size() - left,
            raw.size()));
    }

    return raw;
}

/// Zdejmuje filtry wierszowe i zostawia same piksele.
///
/// Filtr działa na bajtach, nie na kanałach, i odwołuje się do piksela **po lewej**, czyli
/// o `channels` bajtów wstecz — to jest jedyne miejsce, w którym liczba kanałów ma znaczenie.
void unfilter(
    std::vector<std::uint8_t>& raw,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t channels)
{
    const std::size_t stride = static_cast<std::size_t>(width) * channels;

    for (std::uint32_t row = 0; row < height; ++row)
    {
        const std::size_t line = row * (stride + 1);
        const std::uint8_t filter = raw[line];

        std::uint8_t* current = raw.data() + line + 1;
        const std::uint8_t* previous = row == 0 ? nullptr : raw.data() + (row - 1) * (stride + 1) + 1;

        for (std::size_t index = 0; index < stride; ++index)
        {
            const std::uint8_t left = index >= channels ? current[index - channels] : 0;
            const std::uint8_t above = previous != nullptr ? previous[index] : 0;
            const std::uint8_t corner =
                previous != nullptr && index >= channels ? previous[index - channels] : 0;

            switch (filter)
            {
            case 0:
                break;
            case 1:
                current[index] = static_cast<std::uint8_t>(current[index] + left);
                break;
            case 2:
                current[index] = static_cast<std::uint8_t>(current[index] + above);
                break;
            case 3:
                current[index] = static_cast<std::uint8_t>(
                    current[index] + (static_cast<int>(left) + static_cast<int>(above)) / 2);
                break;
            default:
                current[index] =
                    static_cast<std::uint8_t>(current[index] + paeth(left, above, corner));
                break;
            }
        }
    }
}

} // namespace

std::expected<Image, std::string> decode(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < signature.size()
        || !std::equal(signature.begin(), signature.end(), bytes.begin()))
    {
        return std::unexpected(std::string{"To nie jest plik PNG — nie zgadza się sygnatura."});
    }

    std::expected<Header, std::string> header = std::unexpected(
        std::string{"Plik nie ma nagłówka IHDR."});

    std::vector<std::uint8_t> compressed;

    std::size_t offset = signature.size();

    while (offset + 12 <= bytes.size())
    {
        const std::uint32_t length = read_u32_be(bytes, offset);

        if (offset + 12 + length > bytes.size())
        {
            return std::unexpected(std::format(
                "Plik urywa się w środku bloku zaczynającego się na bajcie {}.",
                offset));
        }

        const std::string_view name(reinterpret_cast<const char*>(bytes.data() + offset + 4), 4);
        const std::span<const std::uint8_t> data = bytes.subspan(offset + 8, length);

        const std::uint32_t stored = read_u32_be(bytes, offset + 8 + length);

        // Suma kontrolna liczona jest z nazwy bloku **razem z jego treścią**, więc idzie
        // z jednego wskaźnika przez oba pola. Kosztuje jedno przejście po pliku i wyłapuje
        // plik urwany przy kopiowaniu — czyli ten przypadek, w którym obrazek otwiera się
        // w podglądzie, a mapa wychodzi z niego niepełna.
        const std::uint32_t computed = static_cast<std::uint32_t>(
            crc32(crc32(0, nullptr, 0), bytes.data() + offset + 4, 4 + length));

        if (stored != computed)
        {
            return std::unexpected(std::format(
                "Blok '{}' ma niezgodną sumę kontrolną — plik jest uszkodzony.",
                name));
        }

        if (name == "IHDR")
        {
            header = parse_header(data);

            if (!header)
            {
                return std::unexpected(header.error());
            }
        }
        else if (name == "IDAT")
        {
            if (!header)
            {
                return std::unexpected(std::string{"Blok IDAT stoi przed IHDR."});
            }

            compressed.insert(compressed.end(), data.begin(), data.end());
        }
        else if (name == "IEND")
        {
            break;
        }

        offset += 12 + length;
    }

    if (!header)
    {
        return std::unexpected(header.error());
    }

    if (compressed.empty())
    {
        return std::unexpected(std::string{"Plik nie zawiera danych obrazu (brak IDAT)."});
    }

    const std::size_t channels = header->colour_type == 6 ? 4u : 3u;
    const std::size_t stride = static_cast<std::size_t>(header->width) * channels;

    std::expected<std::vector<std::uint8_t>, std::string> raw =
        inflate_all(compressed, (stride + 1) * header->height);

    if (!raw)
    {
        return std::unexpected(raw.error());
    }

    unfilter(*raw, header->width, header->height, channels);

    Image image;
    image.width = header->width;
    image.height = header->height;
    image.rgb.resize(static_cast<std::size_t>(header->width) * header->height * 3);

    for (std::uint32_t row = 0; row < header->height; ++row)
    {
        const std::uint8_t* line = raw->data() + row * (stride + 1) + 1;

        for (std::uint32_t column = 0; column < header->width; ++column)
        {
            const std::size_t target = (static_cast<std::size_t>(row) * header->width + column) * 3;

            image.rgb[target] = line[column * channels];
            image.rgb[target + 1] = line[column * channels + 1];
            image.rgb[target + 2] = line[column * channels + 2];
        }
    }

    return image;
}

std::expected<Image, std::string> read_file(const std::filesystem::path& path)
{
    std::error_code error;

    const std::uintmax_t size = std::filesystem::file_size(path, error);

    if (error)
    {
        return std::unexpected(
            std::format("Nie udało się otworzyć '{}': {}.", path.string(), error.message()));
    }

    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected(std::format("Nie udało się otworzyć '{}'.", path.string()));
    }

    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));

    if (!bytes.empty()
        && !file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    {
        return std::unexpected(std::format("Plik '{}' urwał się przy czytaniu.", path.string()));
    }

    std::expected<Image, std::string> image = decode(bytes);

    if (!image)
    {
        return std::unexpected(std::format("Plik '{}': {}", path.string(), image.error()));
    }

    return image;
}

} // namespace gs::png
