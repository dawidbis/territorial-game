#include "png.hpp"

#include <zlib.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

// Czytnik PNG. Testy budują pliki bajt po bajcie, bo tylko wtedy da się sprawdzić rzeczy,
// których żaden edytor graficzny nie wyprodukuje na życzenie: uszkodzoną sumę kontrolną,
// 16 bitów na kanał albo wiersz zapisany filtrem Sub.
namespace
{

void put_u32_be(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void put_chunk(
    std::vector<std::uint8_t>& out,
    std::string_view type,
    const std::vector<std::uint8_t>& data)
{
    put_u32_be(out, static_cast<std::uint32_t>(data.size()));

    const std::size_t start = out.size();

    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), data.begin(), data.end());

    put_u32_be(
        out,
        static_cast<std::uint32_t>(
            crc32(crc32(0, nullptr, 0), out.data() + start, static_cast<uInt>(4 + data.size()))));
}

std::vector<std::uint8_t> deflate_bytes(const std::vector<std::uint8_t>& raw)
{
    uLongf size = compressBound(static_cast<uLong>(raw.size()));

    std::vector<std::uint8_t> compressed(size);

    EXPECT_EQ(compress(compressed.data(), &size, raw.data(), static_cast<uLong>(raw.size())), Z_OK);

    compressed.resize(size);

    return compressed;
}

/// Składa plik PNG z gotowych wierszy — pierwszy bajt każdego z nich to numer filtra.
std::vector<std::uint8_t> make_png(
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& raw,
    std::uint8_t bit_depth = 8,
    std::uint8_t colour_type = 2)
{
    std::vector<std::uint8_t> file{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> header;
    put_u32_be(header, width);
    put_u32_be(header, height);
    header.push_back(bit_depth);
    header.push_back(colour_type);
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);

    put_chunk(file, "IHDR", header);
    put_chunk(file, "IDAT", deflate_bytes(raw));
    put_chunk(file, "IEND", {});

    return file;
}

} // namespace

TEST(PngTest, DecodesAnUnfilteredRgbImage)
{
    // Dwa piksele w wierszu, dwa wiersze; filtr 0, czyli bajty wprost.
    const std::vector<std::uint8_t> raw{
        0, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
        0, 0xFF, 0xFF, 0x00, 0x80, 0x80, 0x80};

    const std::expected<gs::png::Image, std::string> image = gs::png::decode(make_png(2, 2, raw));

    ASSERT_TRUE(image.has_value()) << (image ? "" : image.error());

    EXPECT_EQ(image->width, 2u);
    EXPECT_EQ(image->height, 2u);

    const std::vector<std::uint8_t> expected{
        0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0x80, 0x80, 0x80};

    EXPECT_EQ(image->rgb, expected);
}

// Filtry wierszowe to jedyna część czytnika, która robi coś więcej niż przepisywanie bajtów.
// Wiersz zapisany filtrem Sub trzyma różnice względem piksela po lewej.
TEST(PngTest, UndoesTheSubFilter)
{
    const std::vector<std::uint8_t> raw{1, 0x10, 0x20, 0x30, 0x05, 0x05, 0x05};

    const std::expected<gs::png::Image, std::string> image = gs::png::decode(make_png(2, 1, raw));

    ASSERT_TRUE(image.has_value()) << (image ? "" : image.error());

    const std::vector<std::uint8_t> expected{0x10, 0x20, 0x30, 0x15, 0x25, 0x35};

    EXPECT_EQ(image->rgb, expected);
}

TEST(PngTest, UndoesTheUpFilter)
{
    const std::vector<std::uint8_t> raw{0, 0x10, 0x20, 0x30, 2, 0x01, 0x02, 0x03};

    const std::expected<gs::png::Image, std::string> image = gs::png::decode(make_png(1, 2, raw));

    ASSERT_TRUE(image.has_value()) << (image ? "" : image.error());

    const std::vector<std::uint8_t> expected{0x10, 0x20, 0x30, 0x11, 0x22, 0x33};

    EXPECT_EQ(image->rgb, expected);
}

TEST(PngTest, DropsTheAlphaChannel)
{
    const std::vector<std::uint8_t> raw{0, 0x00, 0x00, 0xFF, 0x40};

    const std::expected<gs::png::Image, std::string> image =
        gs::png::decode(make_png(1, 1, raw, 8, 6));

    ASSERT_TRUE(image.has_value()) << (image ? "" : image.error());

    const std::vector<std::uint8_t> expected{0x00, 0x00, 0xFF};

    EXPECT_EQ(image->rgb, expected);
}

TEST(PngTest, RefusesSixteenBitsPerChannel)
{
    const std::vector<std::uint8_t> raw(13, 0);

    EXPECT_FALSE(gs::png::decode(make_png(2, 1, raw, 16, 2)).has_value());
}

TEST(PngTest, RefusesAPaletteImage)
{
    const std::vector<std::uint8_t> raw{0, 0x00};

    EXPECT_FALSE(gs::png::decode(make_png(1, 1, raw, 8, 3)).has_value());
}

// Plik urwany albo nadpisany przy kopiowaniu otwiera się w podglądzie, a mapa wychodzi
// z niego niepełna. Suma kontrolna bloku łapie to przy konwersji.
TEST(PngTest, RefusesAChunkWithABrokenChecksum)
{
    std::vector<std::uint8_t> file = make_png(1, 1, {0, 0x00, 0x00, 0xFF});

    file[file.size() - 5] ^= 0xFF;

    EXPECT_FALSE(gs::png::decode(file).has_value());
}

TEST(PngTest, RefusesSomethingThatIsNotAPngAtAll)
{
    const std::vector<std::uint8_t> bytes{'G', 'I', 'F', '8', '9', 'a'};

    EXPECT_FALSE(gs::png::decode(bytes).has_value());
}
