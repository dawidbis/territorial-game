#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

/// Klucz OpenSSL-a zadeklarowany wprzód, żeby nagłówki `<openssl/...>` nie rozlewały się
/// po całym projekcie. To jest dokładnie ten typ, który kryje się pod `EVP_PKEY`.
struct evp_pkey_st;

namespace gs
{

/// Powód odrzucenia biletu.
///
/// Jeden zestaw dla logów i dla kodu zamknięcia WebSocketa, ale **nie dla klienta** —
/// gracz dostaje jedno „bilet odrzucony". Rozróżnienie „zły podpis" od „nie ten mecz"
/// mówiłoby próbującemu, jak blisko jest celu.
enum class TicketError
{
    malformed,
    unsupported_algorithm,
    bad_signature,
    expired,
    wrong_match,
    bad_slot,
    replayed
};

std::string_view describe(TicketError error);

/// Zweryfikowana zawartość biletu (§5③).
struct Ticket
{
    std::string player_id;

    std::string match_id;

    std::uint8_t slot = 0;

    std::string nonce;

    std::chrono::system_clock::time_point expires_at{};
};

/// Weryfikuje bilety meczowe offline, bez jednego zapytania do meta (§4.3).
///
/// Restart meta nie ma prawa zerwać trwających meczów, więc klucz publiczny wczytywany
/// jest raz przy starcie procesu i nic po drodze nie odpytuje sieci.
///
/// Klasa jest **stanowa**: pamięta zużyte `nonce`. Jednorazowość biletu rozwiązuje się
/// dzięki D7 — jeden proces obsługuje jeden mecz, więc pamięć o zużytych biletach ginie
/// razem z nim i nie ma czego synchronizować. Zbiór rośnie o wpis na wejście gracza,
/// czyli o rzędy wielkości mniej, niż trwa mecz.
class TicketVerifier
{
public:
    /// Zapas na rozjazd zegarów meta i tej maszyny.
    ///
    /// Pięć sekund przy sześćdziesięciosekundowym życiu biletu: dość, by przeżyć zwykłą
    /// niedokładność NTP, za mało, by wydłużyć okno użycia przechwyconego biletu.
    static constexpr std::chrono::seconds clock_skew{5};

    static std::expected<TicketVerifier, std::string> from_pem(
        std::string_view public_key_pem,
        std::string match_id,
        std::uint32_t max_actors);

    static std::expected<TicketVerifier, std::string> from_pem_file(
        const std::string& path,
        std::string match_id,
        std::uint32_t max_actors);

    /// Sprawdza bilet i zużywa jego `nonce`.
    ///
    /// Kolejność nie jest przypadkowa: **najpierw podpis**, dopiero potem cokolwiek
    /// z ładunku. Claim przeczytany przed weryfikacją podpisu jest danymi od obcego.
    std::expected<Ticket, TicketError> verify(
        std::string_view token,
        std::chrono::system_clock::time_point now);

    /// Ilu graczy weszło tym weryfikatorem. Wyłącznie do logów i testów.
    std::size_t admitted() const noexcept
    {
        return used_nonces.size();
    }

    /// Mecz obsługiwany przez ten proces — ta sama wartość musi stać w ścieżce upgrade'u.
    const std::string& match() const noexcept
    {
        return match_id;
    }

    std::uint32_t actor_limit() const noexcept
    {
        return max_actors;
    }

private:
    struct KeyDeleter
    {
        void operator()(evp_pkey_st* key) const noexcept;
    };

    TicketVerifier(evp_pkey_st* key, std::string match_id, std::uint32_t max_actors);

    std::unique_ptr<evp_pkey_st, KeyDeleter> key;

    std::string match_id;

    std::uint32_t max_actors;

    std::unordered_set<std::string> used_nonces;
};

} // namespace gs
