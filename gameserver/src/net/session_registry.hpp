#pragma once

#include "net/session.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gs
{

/// Żywe sesje meczu.
///
/// Jedyne miejsce, które wie, komu wysłać snapshot. Bez muteksów i bez kopiowania bufora:
/// wszystko dzieje się na jednym wątku, a ramka jest współdzielona przez `shared_ptr`.
///
/// Osobno od `Session`, bo to jest **lista połączeń**, a nie połączenie: sesja czyta gniazdo
/// i gada protokołem, rejestr odpowiada wyłącznie na pytanie „kto jeszcze gra". Trzymane
/// razem dawały nagłówek, w którym pętla meczu musiała przewinąć całą obsługę WebSocketa,
/// żeby dojść do jednej metody, której używa.
class SessionRegistry
{
public:
    void add(const std::shared_ptr<Session>& session);

    void remove(const Session* session);

    /// Rozsyła tę samą ramkę do wszystkich. Złożoność zależy od liczby graczy, nie kafelków.
    ///
    /// Stała referencja, bo kopię i tak robi dopiero `Session::send` — po jednej na gracza,
    /// i to jest cała cena rozsyłki. Kopia w parametrze byłaby setną pierwszą.
    void broadcast(const std::shared_ptr<const std::string>& frame);

    /// Wysyła każdemu ramkę zbudowaną dla **jego** slotu.
    ///
    /// Szablon, a nie `std::function`: to jest ścieżka wykonywana kilka razy na sekundę,
    /// a opakowanie domknięcia potrafi alokować.
    template <typename Build>
    void send_each(Build&& build)
    {
        for (const std::shared_ptr<Session>& session : sessions_)
        {
            session->send(build(session->slot()));
        }
    }

    /// Rozłącza wcześniejsze połączenie na tym samym slocie.
    ///
    /// To jest reconnect z D14 w najprostszej postaci: gracz, który wrócił, wypiera swoje
    /// poprzednie wcielenie. Bez tego odświeżenie strony zostawiałoby zombie trzymające slot.
    void drop_previous_on(std::uint8_t slot, const Session* keep);

    std::size_t size() const noexcept
    {
        return sessions_.size();
    }

    void close_all();

private:
    std::vector<std::shared_ptr<Session>> sessions_;
};

} // namespace gs
