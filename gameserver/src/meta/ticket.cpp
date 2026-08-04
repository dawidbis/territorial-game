#include "meta/ticket.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace gs
{
namespace
{

/// Rozmiar jednej połówki podpisu ES256. R i S mają po 256 bitów.
constexpr std::size_t signature_half = 32;

/// Podpis DER dla P-256 mieści się w 72 bajtach; zapas jest darmowy.
constexpr std::size_t max_der_signature = 80;

constexpr auto decode_table = []
{
    std::array<signed char, 256> table{};
    table.fill(-1);

    // Liczniki jako `std::size_t`, żeby indeks powstawał bez rozszerzania `int` do rozmiaru
    // wskaźnika w środku wyrażenia — rzutowanie założone na sumę ukrywa, który składnik
    // faktycznie się rozszerza.
    for (std::size_t index = 0; index < 26; ++index)
    {
        table[static_cast<std::size_t>('A') + index] = static_cast<signed char>(index);
        table[static_cast<std::size_t>('a') + index] = static_cast<signed char>(26 + index);
    }

    for (std::size_t index = 0; index < 10; ++index)
    {
        table[static_cast<std::size_t>('0') + index] = static_cast<signed char>(52 + index);
    }

    table[static_cast<std::size_t>('-')] = 62;
    table[static_cast<std::size_t>('_')] = 63;

    return table;
}();

/// Dekoduje base64url bez wypełnienia — dokładnie tak, jak koduje JWS (RFC 7515).
std::optional<std::string> base64url_decode(std::string_view text)
{
    std::string decoded;
    decoded.reserve(text.size() * 3 / 4);

    std::uint32_t buffer = 0;
    int bits = 0;

    for (const char character : text)
    {
        const signed char value = decode_table[static_cast<unsigned char>(character)];

        if (value < 0)
        {
            return std::nullopt;
        }

        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;
            decoded.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }

    return decoded;
}

std::optional<boost::json::value> parse_json(std::string_view text)
{
    boost::system::error_code error;

    boost::json::value value = boost::json::parse(text, error);

    if (error)
    {
        return std::nullopt;
    }

    return value;
}

bool equals_ignoring_case(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const char a = left[index] >= 'A' && left[index] <= 'Z' ? static_cast<char>(left[index] + 32)
                                                                : left[index];
        const char b = right[index] >= 'A' && right[index] <= 'Z'
            ? static_cast<char>(right[index] + 32)
            : right[index];

        if (a != b)
        {
            return false;
        }
    }

    return true;
}

using SignaturePtr = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;
using DigestContextPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;

/// Sprawdza podpis ES256 nad częścią `nagłówek.ładunek`.
///
/// **Pułapka formatu.** JWS niesie podpis jako surowe `R‖S` (64 bajty, RFC 7518), a OpenSSL
/// przyjmuje wyłącznie strukturę DER. Bez tej konwersji weryfikacja odrzucałaby każdy
/// poprawny bilet — i to jest cały koszt wyboru krzywej P-256 zamiast Ed25519.
bool verify_signature(
    EVP_PKEY* key,
    std::string_view signing_input,
    std::string_view raw_signature)
{
    if (raw_signature.size() != 2 * signature_half)
    {
        return false;
    }

    const auto* bytes = reinterpret_cast<const unsigned char*>(raw_signature.data());

    const SignaturePtr signature(ECDSA_SIG_new(), &ECDSA_SIG_free);

    BIGNUM* r = BN_bin2bn(bytes, signature_half, nullptr);
    BIGNUM* s = BN_bin2bn(bytes + signature_half, signature_half, nullptr);

    if (!signature || r == nullptr || s == nullptr)
    {
        BN_free(r);
        BN_free(s);

        return false;
    }

    // Przejmuje własność obu liczb — po tym wywołaniu zwalnia je ECDSA_SIG_free.
    ECDSA_SIG_set0(signature.get(), r, s);

    std::array<unsigned char, max_der_signature> der{};
    unsigned char* cursor = der.data();

    const int der_length = i2d_ECDSA_SIG(signature.get(), &cursor);

    if (der_length <= 0)
    {
        return false;
    }

    const DigestContextPtr context(EVP_MD_CTX_new(), &EVP_MD_CTX_free);

    if (!context)
    {
        return false;
    }

    if (EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key) != 1)
    {
        return false;
    }

    return EVP_DigestVerify(
               context.get(),
               der.data(),
               static_cast<std::size_t>(der_length),
               reinterpret_cast<const unsigned char*>(signing_input.data()),
               signing_input.size())
        == 1;
}

} // namespace

std::string_view describe(TicketError error)
{
    switch (error)
    {
    case TicketError::malformed:
        return "bilet nie ma kształtu JWT";
    case TicketError::unsupported_algorithm:
        return "bilet podpisany innym algorytmem niż ES256";
    case TicketError::bad_signature:
        return "podpis biletu się nie zgadza";
    case TicketError::expired:
        return "bilet wygasł";
    case TicketError::wrong_match:
        return "bilet dotyczy innego meczu";
    case TicketError::bad_slot:
        return "slot z biletu jest poza zakresem meczu";
    case TicketError::replayed:
        return "bilet został już zużyty";
    }

    return "bilet odrzucony";
}

void TicketVerifier::KeyDeleter::operator()(evp_pkey_st* key) const noexcept
{
    EVP_PKEY_free(key);
}

TicketVerifier::TicketVerifier(evp_pkey_st* key, std::string match_id, std::uint32_t max_actors)
    : key_(key)
    , match_id_(std::move(match_id))
    , max_actors_(max_actors)
{
}

std::expected<TicketVerifier, std::string> TicketVerifier::from_pem(
    std::string_view public_key_pem,
    std::string match_id,
    std::uint32_t max_actors)
{
    const BioPtr bio(
        BIO_new_mem_buf(public_key_pem.data(), static_cast<int>(public_key_pem.size())),
        &BIO_free);

    if (!bio)
    {
        return std::unexpected(std::string{"Nie udało się przygotować bufora na klucz."});
    }

    EVP_PKEY* key = PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr);

    if (key == nullptr)
    {
        return std::unexpected(
            std::string{"Klucz biletów nie daje się wczytać — oczekiwany format to SPKI PEM."});
    }

    // Krzywa jest częścią kontraktu z meta, więc sprawdzana jest przy starcie, a nie przy
    // pierwszym graczu. Proces, który i tak nikogo nie wpuści, ma paść od razu.
    if (EVP_PKEY_get_base_id(key) != EVP_PKEY_EC || EVP_PKEY_get_bits(key) != 256)
    {
        EVP_PKEY_free(key);

        return std::unexpected(
            std::string{"Klucz biletów nie jest kluczem ECDSA P-256 — meta podpisuje ES256."});
    }

    return TicketVerifier(key, std::move(match_id), max_actors);
}

std::expected<TicketVerifier, std::string> TicketVerifier::from_pem_file(
    const std::string& path,
    std::string match_id,
    std::uint32_t max_actors)
{
    const std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        return std::unexpected("Nie udało się otworzyć pliku klucza biletów: " + path);
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    return from_pem(contents.str(), std::move(match_id), max_actors);
}

std::expected<Ticket, TicketError> TicketVerifier::verify(
    std::string_view token,
    std::chrono::system_clock::time_point now)
{
    const std::size_t first_dot = token.find('.');

    if (first_dot == std::string_view::npos)
    {
        return std::unexpected(TicketError::malformed);
    }

    const std::size_t second_dot = token.find('.', first_dot + 1);

    if (second_dot == std::string_view::npos
        || token.find('.', second_dot + 1) != std::string_view::npos)
    {
        return std::unexpected(TicketError::malformed);
    }

    const std::optional<std::string> header = base64url_decode(token.substr(0, first_dot));
    const std::optional<std::string> signature = base64url_decode(token.substr(second_dot + 1));

    if (!header || !signature)
    {
        return std::unexpected(TicketError::malformed);
    }

    const std::optional<boost::json::value> header_json = parse_json(*header);

    if (!header_json || !header_json->is_object())
    {
        return std::unexpected(TicketError::malformed);
    }

    const boost::json::value* algorithm = header_json->get_object().if_contains("alg");

    if (algorithm == nullptr || !algorithm->is_string()
        || algorithm->get_string() != "ES256")
    {
        return std::unexpected(TicketError::unsupported_algorithm);
    }

    // Podpis PRZED czytaniem ładunku: dopóki nie zgadza się podpis, claimy są danymi
    // od obcego i nie wolno na nich niczego opierać.
    if (!verify_signature(key_.get(), token.substr(0, second_dot), *signature))
    {
        return std::unexpected(TicketError::bad_signature);
    }

    const std::optional<std::string> payload =
        base64url_decode(token.substr(first_dot + 1, second_dot - first_dot - 1));

    if (!payload)
    {
        return std::unexpected(TicketError::malformed);
    }

    const std::optional<boost::json::value> payload_json = parse_json(*payload);

    if (!payload_json || !payload_json->is_object())
    {
        return std::unexpected(TicketError::malformed);
    }

    const boost::json::object& claims = payload_json->get_object();

    const boost::json::value* expiry = claims.if_contains("exp");
    const boost::json::value* player = claims.if_contains("playerId");
    const boost::json::value* match = claims.if_contains("matchId");
    const boost::json::value* slot = claims.if_contains("slot");
    const boost::json::value* nonce = claims.if_contains("nonce");

    if (expiry == nullptr || !expiry->is_int64() || player == nullptr || !player->is_string()
        || match == nullptr || !match->is_string() || slot == nullptr || !slot->is_int64()
        || nonce == nullptr || !nonce->is_string())
    {
        return std::unexpected(TicketError::malformed);
    }

    const auto expires_at =
        std::chrono::system_clock::time_point{std::chrono::seconds{expiry->get_int64()}};

    if (now > expires_at + clock_skew)
    {
        return std::unexpected(TicketError::expired);
    }

    if (!equals_ignoring_case(match->get_string(), match_id_))
    {
        return std::unexpected(TicketError::wrong_match);
    }

    const std::int64_t slot_number = slot->get_int64();

    if (slot_number < 1 || std::cmp_greater(slot_number, max_actors_))
    {
        return std::unexpected(TicketError::bad_slot);
    }

    std::string nonce_value{nonce->get_string()};

    if (used_nonces_.contains(nonce_value))
    {
        return std::unexpected(TicketError::replayed);
    }

    used_nonces_.insert(nonce_value);

    return Ticket{
        std::string{player->get_string()},
        std::string{match->get_string()},
        static_cast<std::uint8_t>(slot_number),
        std::move(nonce_value),
        expires_at};
}

} // namespace gs
