# Dokumentacja aplikacji

Opis **tego, co jest faktycznie zaimplementowane** w repozytorium — funkcjonalności, kontrakty,
konfiguracja, uruchomienie.

**Status:** v1 pre-alpha (meta-serwer + klient; serwer gry — szkielet bez gniazda)
**Aktualność:** 30.07.2026

> **Relacja do drugiego dokumentu.** [architektura-gry-terytorialnej.md](architektura-gry-terytorialnej.md)
> opisuje **projekt docelowy** — symulację w C++, protobuf, delty kafelków, orkiestrację. Ten
> dokument opisuje **stan bieżący kodu**. Gdziekolwiek jest odwołanie w postaci „D11" albo „§5③",
> chodzi o decyzję albo sekcję z tamtego dokumentu.

---

## 1. Czym aplikacja jest dzisiaj

Działającą **poczekalnią przed meczem**: gracz wchodzi na stronę, dostaje tożsamość bez
rejestracji, ustawia nick i kolor, dołącza do lobby i widzi na żywo, kto jeszcze czeka. Gdy licznik
dobija do zera, lobby zamraża roster, zakłada mecz, rozdaje bilety i otwiera następne lobby.

Czego **nie ma**: samego meczu. Nie istnieje mapa, symulacja ani ekspansja terytorium. Ścieżka
startu kończy się w chwili, gdy gracz ma w ręku adres i bilet — pod tym adresem nikt jeszcze nie
nasłuchuje, bo alokator jest atrapą (patrz §4.9).

Serwer gry ma od 30.07.2026 **szkielet**: proces w C++, zegar meczu 10/5 Hz i wspólny kontrakt
protobuf, który generuje się także dla klienta. Gniazda jeszcze nie ma, więc na razie niczego nie
zmienia dla gracza — plan doprowadzenia go do mapy na ekranie opisuje
[plan-serwera-gry.md](plan-serwera-gry.md).

### 1.1 Mapa stanu implementacji

| Obszar | Stan |
|---|---|
| Tożsamość gościa, sesja, token | **gotowe** |
| Profil gracza (nick, kolor HSV) | **gotowe** |
| Lobby — roster, nagłówek, snapshoty na żywo | **gotowe** |
| Kanał realtime (SignalR) + zegar rozgłaszania | **gotowe** |
| Odliczanie + synchronizacja zegara klienta | **gotowe**, włączone |
| Karencja rozłączenia, obsługa wielu kart | **gotowe** |
| Cykl życia lobby (zbieranie → start → zamknięcie) | **gotowe** i osiągalne |
| Start meczu: sloty, zapis meczu, bilety, nowe lobby | **gotowe** (etap 1 planu alokacji) |
| Trasa `/match/:matchId`, guard, ponowne wydanie biletu | **gotowe** |
| Persystencja gracza i meczu (EF Core + SQLite) | **gotowe** |
| Widoki `guide` i `contact` | **zaślepki** („in progres...") |
| Widok meczu | **zaślepka** — adres, licznik biletu, bez mapy |
| Katalog map | jedna pozycja wpisana na sztywno |
| Alokacja procesu game-serwera | **atrapa** — zwraca adres z konfiguracji, nic nie uruchamia |
| Podpis biletu | **brak** — nieprzezroczysty ciąg; decyzja: ECDSA P-256 |
| Serwer gry — szkielet (etap E1) | **gotowe**: proces, opcje, zegar 10/5 Hz, schemat protobuf, CI |
| Serwer gry — gniazdo, świat, mapa | **brak** — etapy E2–E5 planu serwera gry |
| Konta i logowanie | **brak** — tylko goście |
| Testy domeny i warstwy aplikacji | **47 testów, wszystkie zielone** |
| Testy ścieżki meczu w warstwie API | **16 testów, wszystkie zielone** |
| Testy serwera gry | **21 testów, wszystkie zielone** |
| Testy frontendu | **brak** (nietknięty szablon, patrz §8.3) |

---

## 2. Stack i struktura repozytorium

| Warstwa | Technologia |
|---|---|
| Klient | Angular 22 (zoneless, sygnały), TailwindCSS 4, DaisyUI 5, `@microsoft/signalr` 10 |
| Meta-serwer | .NET 10, ASP.NET Core (kontrolery + SignalR), EF Core 10 |
| Baza | **SQLite** (docelowo PostgreSQL) |
| Serwer gry | C++23, Boost.Asio/Beast, Protocol Buffers; CMake + vcpkg (manifest) |
| Testy | xUnit, Shouldly, NSubstitute, `FakeTimeProvider`; GoogleTest po stronie C++ |

```
territorial-game/
├── client/                       Angular
│   └── src/
│       ├── core/                 serwisy, interceptor, guard, funkcje pomocnicze
│       ├── features/             home, lobby, profile, guide, contact
│       ├── layout/nav/           nawigacja + plakietka lobby
│       ├── types/                kontrakty API jako typy TS
│       └── environments/
├── meta/                         rozwiązanie .NET (Territorial.Meta.slnx)
│   ├── src/
│   │   ├── Territorial.Meta.Domain/          czysta domena, zero zależności
│   │   ├── Territorial.Meta.Application/     use case'y, DTO, porty
│   │   ├── Territorial.Meta.Infrastructure/  EF Core, repozytoria, katalog map
│   │   └── Territorial.Meta.Api/             kontrolery, hub, auth, zegar, kompozycja
│   └── tests/
├── gameserver/                   serwer pojedynczego meczu (C++23, CMake + vcpkg)
│   ├── src/app/                  opcje wiersza poleceń, logowanie
│   ├── src/tick/                 zegar meczu (korutyna): 10 Hz sim / 5 Hz wysyłka
│   └── tests/                    GoogleTest
├── proto/                        game.proto — wspólny kontrakt meczu (D2)
└── docs/
```

**Dlaczego `proto/` leży osobno:** schemat należy do serwera gry i do klienta naraz, a trzymany
w katalogu jednej ze stron stałby się jej własnością. Kod generowany nie wchodzi do repozytorium —
po stronie C++ tworzy go CMake, po stronie klienta `npm run proto:gen` (uruchamiane automatycznie
przed `start`, `build` i `test`).

**Kierunek zależności:** `Api → Application → Domain`, `Api → Infrastructure → Application`.
Domena nie zna EF Core ani ASP.NET; `Lobby` i `Player` to zwykłe klasy przyjmujące czas
parametrem, dzięki czemu pełny cykl lobby testuje się w mikrosekundach bez bazy i bez `Sleep`.

**Rygor kompilacji** (`meta/Directory.Build.props`): `TreatWarningsAsErrors`,
`AnalysisLevel=latest-recommended`, `GenerateDocumentationFile`, nullable. Ostrzeżenie analizatora
wywala build. Wersje pakietów żyją wyłącznie w `Directory.Packages.props` (Central Package
Management), razem z dwoma wymuszonymi podniesieniami zależności przechodnich pod CVE.

---

## 3. Przepływ gracza end-to-end

```
① Wejście na stronę
   index.html pokazuje splash → bootstrap Angulara
   provideAppInitializer:  GET /api/players/me           (bez tokenu przy 1. wizycie)
   serwer:                 zakłada gracza-gościa, wystawia token
   klient:                 zapis sesji do localStorage, splash zdjęty

② Podłączenie do huba (od razu, niezależnie od trasy)
   klient → wss  /hubs/lobby?access_token=<jwt>
   serwer → Caller.LobbyHeader                            stan lobby dla obserwatora

③ Dołączenie do lobby
   klient → invoke Join()
   serwer:  czyta nick i kolor z bazy, wpisuje do rostera,
            dodaje połączenie do grupy 'lobby-members'
   serwer → Caller.LobbyRoster + zwraca JoinResult

④ Życie lobby (zegar, 1 Hz, tylko gdy coś się zmieniło)
   serwer → Clients.All.LobbyHeader                       wszyscy połączeni
   serwer → Group('lobby-members').LobbyRoster            wyłącznie członkowie

⑤ Start meczu (licznik dobił do zera, roster niepusty)
   serwer → Clients.All.LobbyHeader   state="Starting"    roster zamrożony
   launcher: sloty → zapis meczu → alokacja → bilety
   serwer → User(playerId).MatchReady                     imiennie, nigdy broadcastem
   serwer → Clients.All.LobbyHeader                       już nowe lobby

⑥ Wejście do meczu
   klient:  zapis biletu w pamięci, nawigacja na /match/{matchId}
   guard:   po odświeżeniu strony dobiera bilet przez POST /api/matches/{id}/ticket

⑦ Wyjście
   klient → invoke Leave()  →  usunięcie z rostera i z grupy
```

Krok ② dzieje się w konstruktorze komponentu `App`, a nie w widoku lobby — połączenie żyje
niezależnie od trasy, więc wejście na profil czy poradnik **nie** wypisuje gracza z lobby.

---

## 4. Funkcjonalności

### 4.1 Tożsamość bez rejestracji

Pierwsze `GET /api/players/me` zakłada gracza. Nie ma ekranu rejestracji, hasła ani przycisku
„zagraj jako gość" — gość jest jedynym rodzajem gracza, jaki dziś istnieje.

Nick i kolor są **wyprowadzone z identyfikatora**, więc od pierwszej chwili mają konkretne
wartości. W systemie nie ma pojęcia „wartość domyślna" ani flagi „czy gracz to zmieniał":

| Element | Reguła |
|---|---|
| `Id` | `Guid.CreateVersion7(now)` — sortowalny po czasie utworzenia |
| Nick | `Guest` + **6 końcowych** znaków identyfikatora, wielkimi literami |
| Odcień | 4 końcowe znaki tego sufiksu czytane jako liczba szesnastkowa, modulo 360 |
| Nasycenie / jasność | stałe 72 / 88 |

Znaki **końcowe**, nie początkowe: pierwsze 12 znaków UUIDv7 to 48-bitowy znacznik czasu,
identyczny dla wszystkich graczy przez kilkadziesiąt dni. Losowość siedzi w końcówce.

Encja nazywa się `Player`, nie `User`, celowo — konto logowania dojdzie później jako osobna encja
dowiązana do gracza, dzięki czemu gość zakładający konto zachowa nick, kolor i historię.

**Znacznik ostatniej wizyty.** `Player.Touch(now)` przesuwa `LastSeenAt` z ziarnistością **5 minut**
i zwraca `bool`; warstwa aplikacji zapisuje zmiany tylko wtedy, gdy znacznik faktycznie drgnął.
Typowe wejście na stronę nie generuje `UPDATE`.

### 4.2 Sesja i token

Token JWT **jest** tożsamością gościa — zastąpił identyfikator trzymany wcześniej w `localStorage`.

| Parametr | Wartość |
|---|---|
| Algorytm | HS256, klucz symetryczny (wystawia i weryfikuje ten sam proces) |
| Zawartość | **wyłącznie** `sub` (identyfikator gracza) + `jti` |
| Czas życia | 30 dni |
| Issuer / audience | `territorial-meta` / `territorial-client` |
| Tolerancja zegara | 30 s |

**Dlaczego 30 dni, a nie kwadrans.** Token jest tożsamością, nie poświadczeniem sesji. Klient
odświeża go przy każdym starcie aplikacji (`GET /api/players/me` zwraca świeży token za każdym
razem), więc aktywny gracz nigdy nie zbliża się do wygaśnięcia — i cała maszyneria
refresh-tokenów jest zbędna. Krótki TTL wraca razem z prawdziwym logowaniem.

**Dlaczego w tokenie nie ma nicku.** Kusi, żeby dołożyć nick i kolor i oszczędzić zapytanie do bazy
przy dołączaniu do lobby. Wtedy jednak zmiana nicku nie byłaby widoczna w rosterze aż do
wygaśnięcia tokenu. Dołączenie zdarza się rzadko, więc jedno zapytanie jest tańsze niż nieaktualne
dane pokazywane innym.

**Po stronie klienta:** sesja w sygnale, kopia w `localStorage` pod kluczem `player-session`
(stary klucz `player` jest przy odczycie usuwany). Sesja bez tokenu jest traktowana jak brak
zapisu. Ten sam token karmi **oba transporty**: interceptor HTTP dokleja `Authorization: Bearer`
do żądań na adres API, a fabryka SignalR czyta go z sygnału przy każdym (re)połączeniu.

**Odporność na brakującego gracza.** Żądanie z ważnym tokenem wskazującym gracza, którego nie ma
w bazie (skasowany plik bazy w dev), nie kończy się błędem — zakładany jest nowy gość, a token
wystawiany na identyfikator **zwrócony przez use case**, nie na ten z żądania.

Na hubie ten sam przypadek daje `JoinResult.UnknownPlayer`, a klient odzyskuje się sam: pobiera
nową sesję, **zrywa połączenie** i łączy się ponownie. Zerwanie jest konieczne, nie ostrożnościowe
— tożsamość połączenia ustala się raz, na handshake'u, więc dopóki gniazdo żyje, hub widzi tego
samego nieistniejącego gracza i samo ponowienie `Join` nic by nie dało. Ponowienie jest
**jednokrotne**: gdyby świeżo wydana tożsamość też okazała się nieznana, pętla odnawiałaby sesję
w kółko.

### 4.3 Profil — nick i kolor

Widok `/profile`: pole nicku, podgląd koloru i trzy suwaki HSV (odcień 0–359, nasycenie i jasność
0–100). Tory suwaków nasycenia i jasności są gradientami liczonymi z bieżącego koloru, więc
pokazują, co się stanie po przesunięciu. Stopka raportuje stan: `zsynchronizowano` /
`niezapisane zmiany` / `zapisywanie...` / komunikat błędu; `zapisz` jest aktywne tylko przy
zmianach i poprawnym nicku.

**Walidacja nicku — trzy warstwy, celowo:**

| Warstwa | Mechanizm | Rola |
|---|---|---|
| Klient | `/^[\p{L}\p{N}_-]{3,20}$/u` | natychmiastowa informacja zwrotna |
| Kontrakt HTTP | `[Required]`, `[StringLength(3, 20)]` | odsianie oczywistych błędów |
| Domena | `Nickname.TryCreate` | ostateczne słowo |

Kontroler używa `TryCreate`, nie `Create`, i przy odrzuceniu zwraca **400** z komunikatem, a nie
500. Powód jest konkretny: atrybuty walidacyjne widzą tekst surowy, domena widzi tekst
**przycięty**. `"  ab  "` ma sześć znaków, więc `StringLength` przepuszcza, a `Nickname` odrzuca.
Zakresy HSV pilnowane są atrybutami `[Range]` wyprowadzonymi ze stałych domenowych, więc
`HsvColor.Create` nie ma jak rzucić.

**Propagacja do lobby.** Roster trzyma **kopię** profilu z chwili dołączenia, więc gracz, który
zmienił nick siedząc w lobby, byłby dla pozostałych nadal podpisany po staremu. `PUT` woła
`CurrentLobby.RefreshPlayer`, które przepisuje nick i kolor do rostera bez ruszania `JoinedAt`
(zmiana nicku nie przesuwa nikogo na koniec listy). Operacja jest cicha, gdy gracza w lobby nie
ma; rozgłoszeniem zajmuje się zegar.

**HSV, nie RGB — i przeliczenie na kliencie.** CSS nie zna `hsv()`, więc `hsvToCss` przelicza na
HSL. Bez tego HSV(0, 100, 100) — czysta czerwień — wyszłoby białe.

### 4.4 Lobby: jedno, w pamięci, snapshotami

W systemie istnieje **dokładnie jedno** aktywne lobby, trzymane w singletonie `CurrentLobby`.

**Lobby nie jest encją bazodanową** i nie ma odpowiednika w EF Core. Jest jedno, żyje
kilkadziesiąt sekund i zmienia się przy każdym dołączeniu — trwały zapis oznaczałby `INSERT` na
join i odczyt z powrotem przez zegar, nie rozwiązując przy tym jedynego problemu, który by go
usprawiedliwiał (wiele instancji API; od tego jest backplane). Do bazy trafi dopiero nieodwracalny
wynik, czyli mecz.

**Snapshoty, nie zdarzenia.** Serwer nie wysyła `PlayerJoined` / `PlayerLeft` (jak zapowiadał
§5② dokumentu architektury), tylko **pełny stan** przy każdej zmianie. Zdarzenia zmuszałyby
klienta do utrzymywania własnego reducera, który może rozjechać się ze stanem serwera — i wtedy
trzeba dorabiać resync. Stan lobby ma kilka kilobajtów, więc wysyłanie go w całości jest darmowe,
a klient zostaje czystą funkcją renderującą. To ta sama logika co D4 i D6, zastosowana do lobby.

**Dwa strumienie, dwie grupy odbiorców** — to jest reguła zapisana w jednym miejscu
(`LobbyBroadcaster`):

| Wiadomość | Odbiorcy | Zawartość |
|---|---|---|
| `LobbyHeader` | **wszyscy** połączeni | mapa, tryb, licznik graczy i botów, odliczanie, czas serwera |
| `LobbyRoster` | tylko grupa `lobby-members` | pełna lista graczy z nickami i kolorami |

Pełny roster przy stu graczach to kilka kilobajtów. Rozsyłanie go każdemu, kto tylko otworzył
stronę główną, byłoby setkami kilobajtów na sekundę — a strona główna pokazuje wyłącznie liczby
z nagłówka.

`LobbyRoster` niesie `lobbyId`. Klient odrzuca roster nienależący do oglądanego lobby, bo bez tego
wiadomość wyprzedzająca nagłówek pokazałaby listę z poprzedniej rundy.

**Współbieżność.** Zwykły `Lock` wokół każdej mutacji. Pod lockiem dzieje się wyłącznie mutacja
i zbudowanie snapshotu — nigdy wysyłka ani żadne `await`.

**Skala.** Stan w pamięci oznacza **jedną instancję API**. Przy skali z §7 dokumentu architektury
to właściwy wybór, ale poziomo się nie skaluje: przy wielu instancjach dochodzi backplane SignalR
i wybór lidera dla zegara.

### 4.5 Zegar: jedno miejsce rozgłaszania

`LobbyClock` to `BackgroundService` tykający raz na sekundę (`PeriodicTimer` karmiony
`TimeProvider`). Każdy tik: zamiata wygasłe karencje, popycha lobby, rozsyła stan **jeśli coś się
zmieniło**.

**Hub niczego nie rozgłasza.** Odpowiada wyłącznie wołającemu; mutacje w `CurrentLobby` jedynie
podnoszą flagę `dirty`. Konsekwencje:

- **Coalescing za darmo** — dziesięć dołączeń w tej samej sekundzie to jedna wiadomość do
  wszystkich zamiast dziesięciu.
- **Znika klasa nadużyć** polegająca na klikaniu join/leave w pętli.
- Koszt: do sekundy opóźnienia, zanim pozostali zobaczą nowego gracza. W lobby bez znaczenia.

Wyjątkiem jest roster dla samego wołającego `Join` — dostaje go natychmiast, bo nie ma sensu kazać
mu czekać do sekundy na zobaczenie, do czego dołączył.

**Odporność.** Tik jest opakowany w `try/catch` i loguje błąd — inaczej jedna nieudana wysyłka
zatrzymałaby odliczanie dla całego serwisu aż do restartu. Decyzja o starcie zależy wyłącznie od
`StartsAt`, a nie od odstępu między wywołaniami, więc spóźniony albo zdublowany tik niczego nie
psuje. `TimeProvider` w konstruktorze pozwala w testach przewinąć całą dobę pracy zegara bez
czekania.

### 4.6 Odliczanie i zegar serwera

Serwer wysyła **chwilę startu** (`startsAt`, ISO), nie „pozostało N sekund". Licznik sekundowy
trzeba by rozsyłać sześćdziesiąt razy na lobby, a i tak byłby przestarzały o RTT. Absolutna
chwila leci raz, klient odejmuje ją lokalnie, a reconnect nie wymaga niczego dodatkowego — snapshot
niesie komplet informacji.

Każdy nagłówek niesie też `serverNow`. `ServerClock` liczy z niego **stałe przesunięcie** względem
zegara lokalnego i odświeża czas co 250 ms (jeden interwał na całą aplikację, nie po jednym na
komponent). Dzięki temu odliczanie działa poprawnie także u gracza z rozjechanym zegarem systemowym.

**Odliczanie zatrzymane.** Gdy `startsAt` jest `null`, licznik stoi — i wtedy, i tylko wtedy,
niepuste jest `frozenSeconds` z wartością, na której stoi. Klient odróżnia oba przypadki
etykietą: `starting in...` kontra `standby`, plus komunikat dla czytników ekranu.

Licznik pokazywany jest w trzech miejscach — na stronie głównej, w widoku lobby i na plakietce
w nawigacji — i wszystkie trzy czytają ten sam sygnał.

### 4.7 Karencja rozłączenia i wiele kart

Dwa problemy, dwa mechanizmy:

**① Członkostwo kluczowane graczem, nie połączeniem.** `CurrentLobby` liczy połączenia na gracza.
Bez tego druga karta przeglądarki byłaby drugim graczem, a zamknięcie jednej z nich wyrzucałoby
go z lobby mimo wciąż otwartej drugiej.

**② Karencja 5 sekund od utraty *ostatniego* połączenia.** Bez niej odświeżenie strony (F5)
wypisuje gracza z lobby i wpisuje z powrotem ułamek sekundy później, co u wszystkich pozostałych
objawia się mignięciem listy. Przerwa w sieci wygląda tak samo. Powrót w oknie karencji odwołuje
skreślenie, a roster nigdy nie drgnął — nie ma czego rozgłaszać.

`Leave` na życzenie gracza działa **natychmiast**, bez karencji: to jawna decyzja, nie awaria.

**Powrót po stronie klienta.** `LobbyHub` pamięta w sygnale `membershipWanted`, że gracz chce
siedzieć w lobby, i ponawia `Join` po każdym odzyskaniu połączenia oraz po zmianie `lobbyId`.
Poza `withAutomaticReconnect` (które ma skończoną liczbę prób) działa własna pętla ponawiania co
3 s, więc klient wraca także po długiej przerwie. Samo `Join` jest po stronie serwera
idempotentne, więc wejście na trasę `/lobby` — także przez F5 albo wklejenie adresu — po prostu
wraca do lobby.

### 4.8 Cykl życia lobby

```
Gathering ──(termin, roster niepusty)──> Starting ──> Closed ──> [nowe lobby: Gathering]
    ↑                                                                      │
    └──────────────(termin, roster pusty: okno od nowa)────────────────────┘
```

| Stan | Znaczenie |
|---|---|
| `Gathering` | zbiera graczy — jedyny stan, w którym wolno dołączyć |
| `Starting` | roster zamrożony, mecz jest oddawany game-serwerowi |
| `Closed` | mecz oddany, lobby martwe i zastępowane nowym |

Przejścia są jednokierunkowe — lobby nigdy nie wraca do wcześniejszego stanu, zamiast tego
powstaje nowe (D7: ograniczony czas życia zamiast migracji stanu).

**Puste lobby nie startuje.** Zamiast blokować się w oczekiwaniu na pierwszego gracza, okno
startuje od nowa, mierzone **od teraz**, a nie od przegapionej chwili — licznik biegnie cyklicznie
zawsze.

**Wyniki próby dołączenia** (`JoinResult`) — klient musi umieć pokazać każdy:

| Wynik | Znaczenie | Komunikat u gracza |
|---|---|---|
| `Joined` | wszedł do rostera; **jedyny** zmieniający stan | — |
| `AlreadyJoined` | już był; odświeżono profil, kolejność bez zmian | — |
| `Full` | komplet aktorów | „Lobby jest pełne. Poczekaj na następne…" |
| `NotGathering` | lobby w fazie startu | „To lobby weszło już w fazę startu…" |
| `UnknownPlayer` | token nie wskazuje istniejącego gracza | brak — klient naprawia to sam, patrz §4.2 |
| `Offline` | stan klienta, nie odpowiedź serwera | „Brak połączenia z serwerem…" |

`UnknownPlayer` jest jedyną wartością, której domena nie produkuje — o tym, że gracza nie ma
w bazie, wie dopiero hub. Mieszka w tym samym enumie, bo to jeden kontrakt odpowiedzi dla klienta,
a nie sygnatura jednej metody.

**Co dzieje się na końcu tego cyklu.** Gdy lobby dobija do terminu, zegar rozgłasza nagłówek ze
stanem `Starting` i oddaje zamrożony roster launcherowi — dalszy ciąg opisuje §4.9. Lobby domyka
**launcher**, nie zegar: zamrożona lista musi dożyć do chwili, w której ktoś zrobi z niej mecz.
Roster nowego lobby jest pusty; połączenia zostają, ale członkostwo nie jest dziedziczone —
klient zauważa zmianę `lobbyId` i dołącza ponownie, jeśli gracz nadal siedzi na widoku lobby.

**Cykl jest włączony.** `Lobby:CountdownEnabled` ma wartość `true`, więc lobby otwiera się przez
`Open` z biegnącym licznikiem. Wyłączenie (`false`) nadal działa i zatrzymuje odliczanie na pełnym
oknie — `Starting` staje się wtedy nieosiągalne, co bywa wygodne przy pracy nad czymś innym.

### 4.9 Start meczu: alokacja, bilety, nowe lobby

Etap 1 planu z [plan-alokacji-meczu.md](plan-alokacji-meczu.md): wszystko między „lobby dobiło do
terminu" a „gracz ma w ręku adres i bilet". Game-serwera nadal nie ma — ścieżka kończy się
na bilecie.

```
① zegar        Lobby.Advance → Starting        roster ZAMROŻONY, Join odbija się o NotGathering
               rozgłasza nagłówek state="Starting"
               wpisuje roster do MatchStartChannel i wraca do tykania

② launcher     Match.Create: sloty 1..N ludziom w kolejności rostera, N+1..MaxActors botom
               zapis Match + MatchParticipant                     ← PRZED rozmową z alokatorem
               IMatchAllocator.AllocateAsync                      ← 3 próby, atrapa w etapie 1
               Match.MarkLive(endpoint)

③ bilety       Clients.User(playerId).MatchReady { matchId, wsUrl, ticket, expiresAt }
               jeden na gracza, nigdy broadcastem

④ domknięcie   CurrentLobby.CloseAndReopen() — ZAWSZE, także po nieudanej alokacji

⑤ klient       MatchGateway zapamiętuje bilet W PAMIĘCI, nawigacja na /match/{matchId}
               guard trasy: brak biletu → POST /api/matches/{id}/ticket → nadal brak → strona główna
```

**Alokacja jest poza taktem zegara.** To najważniejsza decyzja tej ścieżki: alokacja to I/O
z ponowieniami, a każde `await` w tiku zatrzymuje rozgłaszanie stanu **dla całego serwisu**.
Dodatkowo zegar łapie każdy wyjątek i tylko go loguje — nieudany start zniknąłby bez śladu
w stanie. Zegar wpisuje więc żądanie do kanału (`MatchStartChannel`, `Channel` bez limitu, jeden
producent) i wraca; resztą zajmuje się `MatchLauncher : BackgroundService`.

**Kolejność w tiku ma znaczenie**: nagłówek `Starting` wychodzi **przed** wpisem do kanału. Przy
szybkiej alokacji launcher zdążyłby inaczej rozgłosić nowe lobby jako pierwszy, a gracze
zobaczyliby start poprzedniego już po otwarciu następnego. Wpis do kanału jest za to poza blokiem
`try` rozgłaszania — nieudana wysyłka nie może zjeść startu, bo lobby jest już zamrożone i tylko
launcher potrafi je z tego stanu wyprowadzić.

**Bilet nie idzie broadcastem.** `Clients.User(...)` wymaga własnego `IUserIdProvider`: domyślny
czyta `ClaimTypes.NameIdentifier`, a tożsamość gracza siedzi w claimie `sub` (`MapInboundClaims`
jest wyłączone). Bez `PlayerUserIdProvider` wysyłka zwracałaby `null` jako identyfikator
i **po cichu nie robiła nic** — najgorszy rodzaj awarii.

**Bilet nie jest jeszcze podpisany.** Nie ma go kto weryfikować, więc kryptografia dawałaby
złudzenie ochrony. Wartość jest nieprzezroczystym ciągiem o docelowym ładunku
(`playerId, matchId, slot, nonce`) i docelowym TTL 60 s; etap 2 podmienia sposób zamknięcia
ładunku, nie kontrakt wiadomości ani kod klienta.

**Sloty (D12).** `0` to pustkowie, `255` woda, `1..254` aktorzy. Ludzie dostają `1..N`
w kolejności rostera — a ta jest stabilna (`JoinedAt`, potem `PlayerId`), więc przypisanie da się
odtworzyć, czego wymaga determinizm z D10. Boty zajmują `N+1..MaxActors` i nie mają wiersza
w bazie: wynikają z liczby ludzi i sufitu mapy, więc nie ma czego synchronizować. Slot jest
**zapisany** na uczestniku, a nie wyliczany ponownie — reguła może się zmienić, a stare mecze mają
zostać czytelne.

**Ziarno** generuje meta z CSPRNG i zapisuje na meczu. Bez zapisanego ziarna replay z D10 nie
istnieje, a przewidywalne ziarno byłoby przewagą w rozgrywce (zachowanie botów).

**Macierz awarii**

| Awaria | Zachowanie |
|---|---|
| Alokator milczy albo brak pojemności | 3 próby, potem `Match.State = Failed`, `MatchStartFailed` do członków lobby, nowe lobby |
| Alokacja udana, bilet niedostarczony do części graczy | pętla leci dalej — slot zostaje zajęty, gracz dobierze bilet ponownie (etap 2) |
| Zapis nieudanego startu też się nie udał | ślad w logu; gracze i tak dostają komunikat |
| Rozgłoszenie nowego lobby nie wyszło | lobby jest już otwarte; klienci zobaczą je przy najbliższej zmianie stanu |
| Restart meta w trakcie alokacji | mecz zostaje w `Allocating` — zamiatanie takich wierszy jeszcze nie istnieje (§9) |

**Ponowne wydanie biletu** (`POST /api/matches/{matchId}/ticket`, §5.1) nie jest dodatkiem, tylko
częścią tej samej ścieżki. Bilet żyje minutę, więc gracz z kartą w tle zdąży przegapić
`MatchReady`; dostarczenie do części graczy mogło się nie udać, a proces trzyma ich sloty; i wreszcie
jest to **dokładnie ten sam kod**, którego wymaga powrót do trwającego meczu po zerwaniu połączenia
(D14). Adres brany jest z zapisanego `Match.WsUrl`, a nie składany na nowo z konfiguracji — regułę
adresu zna alokator i to on ją stosuje, więc drugie miejsce składające adres rozjechałoby się z tym,
co gracz dostał w `MatchReady`.

**Mecze porzucone w trakcie alokacji** zamiata `StaleMatchSweeper` przy starcie procesu: wiersz
w stanie `Allocating` po restarcie nie ma już swojego launchera, więc nikt by go nie domknął.
Bezpieczne wyłącznie przy jednej instancji — druga wymagałaby progu wieku albo dzierżawy, inaczej
oznaczałaby jako nieudane mecze zakładane właśnie przez sąsiada.

**Klient.** Bilet trzymany jest **wyłącznie w pamięci** (`MatchGateway`) — sześćdziesięciosekundowe
poświadczenie nie ma po co trafiać do `localStorage`, a pamięć umiera razem z kartą, co jest tu
zaletą. Obsługa `MatchReady` gasi `membershipWanted` **przed** czymkolwiek innym: nagłówek nowego
lobby może przyjść w tej samej sekundzie, a gracz wpuszczony do meczu nie może wrócić do kolejki na
następny. Potem nawiguje na `/match/{matchId}` — nawigacja wychodzi z serwisu, a nie z widoku lobby,
bo hub żyje w `App` i zaproszenie musi zadziałać także wtedy, gdy gracz czeka na profilu.

Utrata biletu przy odświeżeniu strony **nie jest awarią**: guard trasy dobiera nowy z serwera
i dopiero jego odmowa odsyła gracza na stronę główną. W fazie `Starting` nad licznikiem lobby
pojawia się „allocating server…", żeby zero na zegarze nie wyglądało jak zawieszony serwis.

Sam widok meczu jest zaślepką — pokazuje adres, licznik ważności biletu i przycisk odnowienia.
Tu wejdzie kanwa, worker i strumień protobuf.

### 4.10 Nawigacja i spójność stanu w UI

Pięć tras, wszystkie ładowane leniwie (`loadComponent`), więc wejście na stronę główną nie ściąga
profilu, poradnika ani lobby:

| Trasa | Widok | Uwagi |
|---|---|---|
| `/` | strona główna | wizytówka lobby + licznik + „join lobby"; guard, patrz niżej |
| `/lobby` | lobby | wizytówka + lista graczy + „leave lobby" |
| `/profile` | profil | nick i kolor |
| `/guide` | poradnik | zaślepka |
| `/contact` | kontakt | zaślepka |

**Gracz w lobby nie może wrócić na stronę główną.** Pokazuje ona to samo lobby, tylko bez listy
graczy — dla kogoś, kto już dołączył, jest krokiem wstecz. Rozwiązane trzema warstwami:

1. Pozycja „start" w nawigacji **zmienia nazwę** na „lobby" i prowadzi tam, gdzie gracz jest.
   Przemianowany link, a nie wygaszony: martwy link czyta się jak błąd.
2. Logo prowadzi wtedy również do lobby.
3. Guard `redirectMembersToLobby` domyka pozostałe drogi — wpisany adres, przycisk wstecz, stary
   zakładkowany link.

**Plakietka lobby w nawigacji** (licznik graczy + odliczanie) pokazuje się, gdy gracz jest w lobby
i **nie** patrzy na widok lobby. Jest w nawigacji, a nie w treści podstron, bo to stan globalny,
a nawigacja jest jedynym elementem wspólnym dla wszystkich tras — profil, poradnik i kontakt nie
muszą o niej nic wiedzieć.

**Powrót z profilu** idzie przez historię (`location.back()`), a nie sztywnym linkiem na stronę
główną — ten wyrzucałby gracza z lobby. `ActiveRoute.canGoBack` liczy nawigacje w obrębie
aplikacji, żeby u kogoś, kto wszedł prosto z zakładki, `back()` nie wyprowadziło poza serwis.

**Zoneless.** Aplikacja nie używa Zone.js, więc bieżąca trasa jest udostępniona jako sygnał
(`ActiveRoute`) — odczyt `router.url` prosto w szablonie nie miałby na czym zawiesić detekcji
zmian.

**Przejścia między widokami:** `withViewTransitions({ skipInitialTransition: true })` plus
`PreloadAllModules`. Pierwsza nawigacja nie ma z czego animować, a wstępne ładowanie w czasie
bezczynności zdejmuje oczekiwanie na `import()` ze ścieżki nawigacji — animacja nie ma na co czekać.

**Splash startowy** siedzi w `index.html` i jest usuwany w `provideAppInitializer` po pobraniu
sesji, w `finally` — także gdy pobranie się nie udało, żeby aplikacja nie została na zawsze pod
ekranem ładowania.

**Renderowanie listy graczy.** Kolory rostera liczone są raz na zmianę listy (`computed`), nie przy
każdym cyklu detekcji — zegar odświeża się cztery razy na sekundę, a graczy może być stu. Lista
jest wielokolumnowa i **jako jedyna** przewija się w widoku lobby: strona jako całość nigdy nie
dostaje suwaka.

### 4.11 Warstwa wizualna

Motyw DaisyUI `crt` — zielony monochrom na czerni, monospace, zerowe promienie zaokrągleń.
Efekty w `styles.css`:

| Klasa / efekt | Rola |
|---|---|
| `.crt-glow` | poświata luminoforu (`text-shadow` w `currentColor`, działa na każdym odcieniu) |
| `.crt-panel-glow` | bloom wokół panelu + podświetlenie wnętrza |
| `.crt-grid` | siatka plotera pod podgląd mapy |
| `.crt-range` | suwaki HSV (natywny `range` wymaga pseudo-elementów osobno dla WebKita i Gecko) |
| `.crt-screen::before` | winieta — krawędzie gasną do czerni |
| `.crt-screen::after` | linie ramki co 3 px + migotanie |

Nakładki są `fixed inset-0`, czyli pokrywają dokładnie widoczny obszar i nie powiększają dokumentu
— żadnych pasków przewijania. Migotanie respektuje `prefers-reduced-motion`.

**Dostępność:** nagłówki `sr-only` na widokach, `role="timer"` z etykietą na licznikach,
`role="alert"` na komunikacie o nieudanym dołączeniu, `aria-hidden` na dekoracjach, etykiety
opisowe na plakietce i panelu gracza. `btn-outline` wymaga modyfikatora koloru — bez niego
w tym motywie wychodzi z alfą 0.2 i nie przechodzi kontrastu AA.

### 4.12 Bezpieczeństwo

| Mechanizm | Realizacja |
|---|---|
| Tożsamość | JWT HS256; wcześniejszy nagłówek `X-Player-Id` pozwalał jednym `curl`-em przejąć dowolny profil |
| Klucz podpisu | z user-secrets (dev) / sekretów hosta (prod); **nigdy** w repozytorium |
| Start bez klucza | aplikacja **nie wstaje** — jawny wyjątek z instrukcją `dotnet user-secrets set` |
| Minimalna długość klucza | 32 znaki (HS256 wymaga klucza nie krótszego niż długość skrótu) |
| CORS | jawna lista origin-ów; `AllowCredentials` wymusza SignalR, co wyklucza `AllowAnyOrigin` |
| Autoryzacja | `[Authorize]` na kontrolerze graczy i na hubie; anonimowe są tylko `GET /api/players/me` i `GET /api/lobby` |
| Token na WebSockecie | z query stringu, **zawężone do ścieżek `/hubs`** |
| Połączenie bez tożsamości | `Context.Abort()` w `OnConnectedAsync` |

**Dlaczego tożsamość wyszła z nagłówka do tokenu.** Przeglądarkowe `WebSocket` API nie pozwala
ustawić własnych nagłówków na handshake'u, więc `X-Player-Id` nigdy nie dotarłoby do SignalR.
`ClaimsPrincipalExtensions.GetPlayerId` działa identycznie w kontrolerze (`User`) i w hubie
(`Context.User`) — to jest cały powód tej zmiany. Token w query jest zawężony do `/hubs`, żeby nie
otwierać tej furtki REST-owi, gdzie lądowałby w logach dostępowych proxy.

**Uwaga konfiguracyjna:** `MapInboundClaims = false` jest konieczne — bez tego `sub` zostaje
przemapowane na długi URI `ClaimTypes.NameIdentifier` i `GetPlayerId` nie znajduje claima, który
serwis sam przed chwilą wystawił.

### 4.13 Persystencja

Trzy tabele. Migracje: `20260728210541_InitialCreate`, `20260730141334_AddMatches`.

```
players
  id                TEXT  PK   (GUID, ValueGeneratedNever)
  nickname          TEXT       maxLength 20
  created_at        TEXT
  last_seen_at      TEXT
  color_hue         INTEGER
  color_saturation  INTEGER
  color_value       INTEGER

matches
  id                TEXT  PK   (GUID v7)
  map_id            TEXT
  mode              TEXT       enum jako tekst
  max_actors        INTEGER    sufit aktorów: ludzie + boty
  seed              INTEGER    ziarno symulacji z CSPRNG
  endpoint          TEXT?      host:port procesu; null do zakończenia alokacji
  ws_url            TEXT?      adres publiczny dla klienta; null jak wyżej
  state             TEXT       Allocating | Live | Completed | Failed  (indeks)
  created_at, started_at?, ended_at?  TEXT

match_participants
  (match_id, player_id)  PK, FK → matches ON DELETE CASCADE
  slot              INTEGER    1..254; unikalny w obrębie meczu
  nickname          TEXT       KOPIA z chwili startu
  color_*           INTEGER    kopia jak wyżej
```

**Lobby nie ma tabeli, mecz ma.** Lobby jest jedno, żyje kilkadziesiąt sekund i zmienia się przy
każdym dołączeniu — zapis oznaczałby INSERT na join i odczyt z powrotem przez zegar. Mecz jest
pierwszą rzeczą, która **musi** przetrwać restart: bez niego meta nie odpowie „czy gracz X jest
uczestnikiem meczu Y na slocie S", więc nie wyda ponownie biletu ani nie przyjmie wyniku.

**Nick i kolor uczestnika to kopia**, nie referencja: game-serwer potrzebuje ich do `SlotInfo`
w `MatchInit`, a historia meczów powinna pokazywać nick używany wtedy, nie dzisiejszy.

Dwa rozwiązania warte odnotowania:

- **`HsvColor` jako `ComplexProperty`**, nie owned entity: typy owned muszą być referencyjne,
  a `HsvColor` to `readonly record struct`. Mapuje się na trzy kolumny tej samej tabeli, bez
  sztucznej tożsamości.
- **`Nickname.FromTrusted` przy materializacji**, nie `Create`: walidacja należy do wejścia, nie do
  odczytu. Gdyby reguły nicka kiedyś się zaostrzyły, `Create` wysadzałby **czytanie** istniejących
  wierszy.

### 4.14 Diagnostyka

| Endpoint | Dostępność |
|---|---|
| `GET /api/health` | zawsze |
| `GET /openapi/v1.json` | tylko Development |
| `GET /scalar` (interaktywna dokumentacja API) | tylko Development; przekierowuje na `/scalar/v1` |

Logowanie: `LoggerMessage` z generatorem źródeł (analizator wymusza — `CA1848` traktuje
interpolację w logach jako błąd). W dev włączone logowanie komend EF Core.

---

## 5. Referencja API

### 5.1 REST

Baza: `/api`. Serializacja JSON, camelCase.

#### `GET /api/players/me` — anonimowy

Zwraca profil odwiedzającego, zakładając gracza przy pierwszej wizycie. Jedyny endpoint, którym
wchodzi się do systemu; token wraca przy każdym wywołaniu, więc samo wejście na stronę odnawia
sesję.

```json
{
  "player": { "id": "0198…", "nickname": "GuestA3F19C",
              "color": { "hue": 214, "saturation": 72, "value": 88 } },
  "accessToken": "eyJ…",
  "expiresAt": "2026-08-29T18:22:41.000Z"
}
```

| Kod | Kiedy |
|---|---|
| 200 | zawsze (brak tokenu i nieznany gracz też kończą się sukcesem) |

#### `PUT /api/players/me` — wymaga tokenu

```json
{ "nickname": "Kowalski", "color": { "hue": 120, "saturation": 80, "value": 90 } }
```

Odpowiedź: `PlayerProfileDto` (`{ id, nickname, color }`).

| Kod | Kiedy |
|---|---|
| 200 | zapisano |
| 400 | nick nie przechodzi walidacji albo HSV poza zakresem |
| 401 | brak albo nieprawidłowy token |
| 404 | token ważny, ale gracza nie ma w bazie |

#### `GET /api/lobby` — anonimowy

Nagłówek aktualnego lobby (`LobbyHeaderDto`). Aktualizacje na żywo idą hubem; ten endpoint istnieje
na pierwszy paint i na wypadek środowiska bez WebSocketów. **Rostera tu nie ma** — należy się
wyłącznie graczom, którzy dołączyli.

#### `POST /api/matches/{matchId}/ticket` — wymaga tokenu

Wydaje wołającemu świeży bilet do meczu, w którym gra. Ciało żądania puste.

```json
{
  "ticket": "eyJ…",
  "wsUrl": "wss://localhost:5001/match/019fb3…",
  "expiresAt": "2026-07-30T14:21:03.000Z"
}
```

| Kod | Kiedy |
|---|---|
| 200 | wołający jest uczestnikiem, a mecz jest w stanie `Live` |
| 401 | brak albo nieprawidłowy token |
| 404 | **wszystko inne** — mecz nie istnieje, nie żyje albo wołający w nim nie gra |

Jedno 404 na trzy różne sytuacje jest celowe: rozróżnienie „nie ma takiego meczu" od „nie grasz
w nim" zamieniłoby endpoint w sposób sprawdzania, kto gra w meczu o zgadniętym identyfikatorze,
dostępny dla każdego z tokenem gościa.

`wsUrl` czytany jest **z meczu**, a nie składany na nowo z konfiguracji — patrz §4.9.

#### `GET /api/health`

Standardowy health check ASP.NET Core.

### 5.2 Hub `/hubs/lobby`

Uwierzytelniony (`[Authorize]`). Token w query stringu `access_token`. Protokół JSON, camelCase.

**Klient → serwer**

| Metoda | Zwraca | Opis |
|---|---|---|
| `Join()` | `string` (nazwa `JoinResult`) | dołącza wołającego; **idempotentne** |
| `Leave()` | `void` | wychodzi z lobby, zostawiając połączenie żywe — gracz nadal widzi nagłówek |

**Serwer → klient** (interfejs `ILobbyClient` — literówka w nazwie metody jest błędem kompilacji,
nie ciszą po stronie klienta)

| Metoda | Odbiorcy | Ładunek |
|---|---|---|
| `LobbyHeader(header)` | wszyscy połączeni | `LobbyHeaderDto` |
| `LobbyRoster(roster)` | grupa `lobby-members` | `LobbyRosterDto` |
| `MatchReady(match)` | **wyłącznie jeden gracz** | `MatchReadyDto` |
| `MatchStartFailed(failure)` | grupa `lobby-members` | `MatchStartFailedDto` |

Samo podłączenie **nie** oznacza członkostwa: strona główna łączy się tylko po to, żeby widzieć
nagłówek na żywo.

`MatchReady` adresowane jest po graczu (`Clients.User`), bo bilet jest poświadczeniem na konkretny
slot. Działa to dzięki `PlayerUserIdProvider` czytającemu claim `sub` — patrz §4.9.

### 5.3 Kontrakty

```
LobbyHeaderDto
  lobbyId        Guid
  state          "Gathering" | "Starting" | "Closed"
  mapId          string
  mapName        string
  mode           "Ffa"
  playerCount    int          ludzie w lobby
  maxPlayers     int          sufit aktorów mapy (ludzie + boty)
  botCount       int          wyliczane: max(0, maxPlayers - playerCount)
  startsAt       DateTimeOffset?   null = odliczanie zatrzymane
  frozenSeconds  int?              niepuste dokładnie wtedy, gdy startsAt = null
  serverNow      DateTimeOffset    czas serwera w chwili zbudowania snapshotu

LobbyRosterDto        { lobbyId: Guid, players: LobbyPlayerDto[] }
LobbyPlayerDto        { playerId: Guid, nickname: string, color: HsvColorDto }
PlayerProfileDto      { id: Guid, nickname: string, color: HsvColorDto }
HsvColorDto           { hue: 0..359, saturation: 0..100, value: 0..100 }

MatchReadyDto         { matchId: Guid, wsUrl: string, ticket: string, expiresAt: DateTimeOffset }
MatchStartFailedDto   { reason: string }
```

`MatchReadyDto` jest celowo minimalne: mapa, tick rate i lista slotów przyjdą w `MatchInit` od
game-serwera, żeby nie mieć dwóch źródeł prawdy o tym samym meczu. `MatchStartFailedDto` nie niesie
żadnego szczegółu awarii — adresy wewnętrzne i treści wyjątków zostają w logach.

`state` i `mode` są **tekstem**, a nie liczbą: kontrakt sieciowy pozostaje czytelny, dopisanie
wartości do enuma niczego nie przesuwa, a domena nie musi znać atrybutów serializacji.

Wszystkie kontrakty mają odpowiedniki w `client/src/types/`, pisane ręcznie — generowanie
z OpenAPI (NSwag, §4.1 dokumentu architektury) nie jest jeszcze wprowadzone.

### 5.4 Model domenowy

```
Player                encja trwała
  Id, Nickname, Color, CreatedAt, LastSeenAt
  CreateGuest(now) · Rename · ChangeColor · Touch(now) → bool

Nickname              readonly record struct; 3–20 znaków, litery/cyfry/-/_ , przycinany
HsvColor              readonly record struct; 0..359 / 0..100 / 0..100

Lobby                 stan w pamięci, bez odpowiednika w bazie
  Id, Map, Mode, State, GatheringWindow, StartsAt, CreatedAt
  HumanCount, BotCount, IsFull
  Open · OpenFrozen · Join · Refresh · Leave · Advance · Close · Roster · Contains

LobbyPlayer           record: PlayerId, Nickname, Color, JoinedAt
MapDefinition         record: Id, Name, MaxActors
GameMode              Ffa
LobbyState            Gathering | Starting | Closed
LobbyTick             Idle | WindowReset | Started
JoinResult            Joined | AlreadyJoined | Full | NotGathering

Match                 encja trwała
  Id, MapId, Mode, MaxActors, Seed, Endpoint?, WsUrl?, State,
  CreatedAt, StartedAt?, EndedAt?
  HumanCount, BotCount, IsLive, Participants
  Create(map, mode, seed, roster, now) · MarkLive(endpoint, wsUrl, now) · MarkFailed(now)
  ParticipantOf(playerId) → MatchParticipant?

MatchParticipant      encja trwała: MatchId, PlayerId, Slot, Nickname, Color
MatchState            Allocating | Live | Completed | Failed
ActorSlot             stałe D12: Wilderness=0, FirstActor=1, LastActor=254, Water=255
```

`Roster()` zwraca listę w **stabilnej kolejności** — od najdawniej obecnego, z identyfikatorem jako
rozstrzygnięciem remisu. Sortowanie jest w domenie, a nie w warstwie prezentacji, bo to właściwość
lobby, a nie sposobu jego wyświetlenia; bez tego lista skakałaby graczom przed oczami, kolejność
słownika nie jest gwarantowana.

---

## 6. Testy

### 6.1 Domena i warstwa aplikacji — 47 testów, wszystkie zielone

```bash
dotnet test meta/Territorial.Meta.slnx
```

**`LobbyTests`** — czysta maszyna stanów, bez bazy i bez zegara:

- otwarcie z odliczaniem i bez (`Open` / `OpenFrozen`, w tym „nigdy nie startuje, choćby długo po
  terminie")
- dołączanie: do rostera, idempotentnie (z zachowaniem pozycji przy odświeżeniu profilu),
  odrzucenie przy pełnej mapie i po wejściu w fazę startu
- `Refresh`: aktualizacja bez przesuwania w rosterze, brak zmiany gdy nic się nie różni,
  ignorowanie graczy z zewnątrz
- `Leave` i raportowanie faktycznej zmiany
- `BotCount` dopełniający mapę do `MaxActors` (teoria z zestawem wejść)
- `Advance`: bezczynność przed terminem, restart okna przy pustym lobby (mierzony **od teraz**,
  nie od przegapionej chwili), start przy niepustym, bezczynność po wyjściu z fazy zbierania
- kolejność rostera niezależna od identyfikatorów
- pełny cykl: zbieranie → start → zamknięcie

**`CurrentLobbyTests`** — koordynacja, karencje i coalescing, na `FakeTimeProvider`:

- publikacja dokładnie raz po zmianie, potem cisza
- seria dołączeń zwinięta do jednego snapshotu
- karencja: gracz zostaje w rosterze, wypada po jej upływie, wraca gdy zdąży
- rozłączenie jednej z dwóch kart **nie** rozpoczyna karencji
- `Leave` usuwa natychmiast, bez czekania na karencję
- `RefreshPlayer`: nowy nick widoczny dla wszystkich; cisza dla kogoś spoza lobby
- start: tik oddaje zamrożony roster, a lobby **zostaje** w `Starting`, dopóki ktoś go nie zużyje
- puste lobby restartuje okno zamiast startować; `CloseAndReopen` otwiera puste lobby z nowym `Id`

**`MatchTests`** — przypisanie slotów i przejścia stanu meczu:

- ludzie dostają kolejne sloty w kolejności rostera, zaczynając od `1` (a nie od zarezerwowanego `0`)
- nick i kolor są kopiowane, więc późniejsza zmiana profilu nie przepisuje historii
- boty dopełniają mapę do `MaxActors`
- odrzucenie pustego rostera, rostera większego od mapy i mapy szerszej niż przestrzeń slotów
- `MarkLive` zapisuje oba adresy i chwilę startu, drugie wywołanie rzuca
- `MarkFailed` jest ciche dla meczu, który już żyje — wołane bywa ze ścieżki obsługi awarii
- `ParticipantOf` znajduje slot uczestnika i nic nie zwraca dla kogoś z zewnątrz

### 6.2 Ścieżka meczu w warstwie API — 16 testów, wszystkie zielone

`MatchLauncherTests` składa prawdziwy launcher, prawdziwe lobby, broadcaster i wystawianie biletów;
podstawione są wyłącznie porty na zewnątrz (orkiestrator, baza, transport SignalR). Launcher
wołany jest wprost, bez wątku w tle — test hostowanego serwisu musiałby na coś czekać i migotałby
z powodów niezwiązanych z testowaną logiką.

- bilet trafia do każdego człowieka i **wyłącznie** do niego (`Clients.User`, bez broadcastu)
- mecz jest zapisany, zanim ruszy rozmowa z alokatorem
- adres z alokacji ląduje na meczu, uczestnik dostaje slot `1`
- po starcie otwiera się nowe, puste lobby
- alokacja jest ponawiana przed poddaniem się
- trwała awaria alokacji: `MatchStartFailed` do czekających, mecz w stanie `Failed`, nowe lobby
  mimo wszystko otwarte

`MatchesControllerTests` — ponowne wydanie biletu: uczestnik dostaje świeży bilet i **ten sam**
adres co w `MatchReady`; nieuczestnik, mecz w trakcie alokacji i mecz nieistniejący dają
identyczne 404; żądanie bez tożsamości kończy się 401 i nie dotyka bazy.

`StaleMatchSweeperTests` — mecze zostawione w `Allocating` są zamykane, brak takich nie generuje
zapisu, a padnięta baza nie przewraca startu serwisu (samo zamiatanie wyjątek przepuszcza, połyka
go dopiero hostowany serwis).

Nadal **brak pokrycia** dla uwierzytelniania na poziomie pipeline'u, huba end-to-end i persystencji —
`WebApplicationFactory<Program>` jest podłączona (`InternalsVisibleTo`), ale nikt jej jeszcze
nie używa.

### 6.3 Frontend — brak pokrycia

Jedyny plik testowy (`app.spec.ts`) to **nietknięty szablon** Angulara: sprawdza obecność tekstu
`Hello, client`, którego w szablonie nie ma, i nie dostarcza `provideRouter` wymaganego przez
`App`. Nie jest to zestaw testów, który cokolwiek weryfikuje — do napisania od zera albo do
usunięcia.

---

## 7. Konfiguracja

### 7.1 Meta-serwer

| Klucz | Domyślnie | Znaczenie |
|---|---|---|
| `ConnectionStrings:Meta` | `Data Source=App_Data/territorial.db` | SQLite; w dev osobny plik `-dev` |
| `Cors:AllowedOrigins` | `[]` (dev: `https://localhost:4200`, `http://localhost:4200`) | jawna lista |
| `Jwt:SigningKey` | **brak — wymagane** | ≥ 32 znaki; z user-secrets |
| `Jwt:Issuer` / `Jwt:Audience` | `territorial-meta` / `territorial-client` | |
| `Jwt:Lifetime` | 30 dni | |
| `Lobby:GatheringSeconds` | `60` | długość okna zbierania |
| `Lobby:CountdownEnabled` | `true` | przełącznik cyklu lobby; `false` zatrzymuje licznik |
| `Lobby:DisconnectGraceSeconds` | `5` | karencja po utracie ostatniego połączenia |
| `Match:AllocationAttempts` | `3` | ile prób rozmowy z alokatorem, zanim start uznamy za nieudany |
| `Match:AllocationRetryDelayMilliseconds` | `250` | odstęp między próbami |
| `Match:TicketLifetimeSeconds` | `60` | ważność biletu meczowego |
| `Match:MatchWebSocketBaseUrl` | `wss://localhost:5001/match` | prefiks adresu meczu; pełny to `{prefiks}/{matchId}` |
| `Match:FakeAllocatorEndpoint` | `127.0.0.1:5101` | adres oddawany przez atrapę alokatora |

Profil `https` nasłuchuje na `https://localhost:5001`.

Każdy z tych kluczy da się nadpisać w user-secrets — również `ConnectionStrings:Meta`, jeśli plik
bazy ma leżeć poza katalogiem projektu.

### 7.2 Klient

| Środowisko | `apiUrl` | `hubUrl` |
|---|---|---|
| development | `https://localhost:5001/api/` | `https://localhost:5001/hubs/` |
| production | `/api/` | `/hubs/` |

Serwer deweloperski chodzi na porcie 4200 **po HTTPS**, z certyfikatem z `client/ssl/`
(katalog jest w `.gitignore` — certyfikat trzeba wyeksportować lokalnie).

---

## 8. Uruchomienie lokalne

**① Klucz podpisu JWT** (bez niego API nie wstanie):

```bash
dotnet user-secrets --project meta/src/Territorial.Meta.Api set "Jwt:SigningKey" "<co-najmniej-32-znaki>"
```

**② Baza:**

```bash
dotnet ef database update --project meta/src/Territorial.Meta.Infrastructure --startup-project meta/src/Territorial.Meta.Api
```

**③ Meta-serwer:**

```bash
dotnet run --project meta/src/Territorial.Meta.Api --launch-profile https
```

**④ Klient:**

```bash
npm --prefix client install
```

```bash
npm --prefix client run start
```

Aplikacja: `https://localhost:4200`. Dokumentacja API (Scalar): `https://localhost:5001/scalar`.

Certyfikat deweloperski dla `ng serve --ssl` trzeba wyeksportować do `client/ssl/localhost.pem`
i `localhost.key` — samo `dotnet dev-certs https --trust` nie wystarczy, bo Angular czyta pliki
z dysku.

### 8.1 Serwer gry

Osobny cykl budowania i **na tym etapie niepotrzebny do uruchomienia aplikacji** — proces nie ma
jeszcze gniazda, więc nic do niego nie wchodzi. Wymaga `VCPKG_ROOT` wskazującego instalację vcpkg;
wszystkie polecenia z katalogu `gameserver/`.

```bash
cmake --preset windows-msvc
```

```bash
cmake --build --preset windows-msvc
```

```bash
ctest --preset windows-msvc
```

Pierwsza konfiguracja zaciąga Boost, protobuf i GoogleTest — z pustym cache'em vcpkg to
kilkadziesiąt minut kompilacji, z zapełnionym kilkadziesiąt sekund. Kolejne budowy dotyczą już
tylko naszego kodu.

| Preset | Kiedy |
|---|---|
| `windows-msvc` | terminal i CI — generator Visual Studio znajduje kompilator sam |
| `windows-ninja` | Developer PowerShell, Debug — szybsza budowa przyrostowa |
| `windows-ninja-release` | to samo z optymalizacją, do mierzenia czasu |
| `linux-clang` | platforma docelowa (decyzja 6.5 planu serwera gry) |

Dwa generatory nie są niekonsekwencją: Ninja wymaga kompilatora na ścieżce, czyli środowiska
`vcvars`, a generator Visual Studio znajduje go sam i dzięki temu `cmake --preset` działa
z każdego terminala. **CLion nie używa żadnego z tych presetów** — patrz §8.2.

Test dymny — proces przechodzi 50 tików symulacji (5 sekund) i kończy się sam:

```bash
./build/windows-msvc/RelWithDebInfo/gameserver.exe --match-id 018f3a2b-5c7d-7e91-9a2b-3c4d5e6f7a8b --port 5101 --max-ticks 50
```

### 8.2 Serwer gry w CLion

Projektem CMake jest katalog `gameserver/` i to jego otwiera się w CLion.

**W CLion pracuje się na jego własnym profilu, nie na presecie** — profile z presetów należy
wyłączyć. Powód jest konkretny: dla profilu z presetu CLion podaje pełną ścieżkę do `cl.exe`, ale
**nie wczytuje środowiska Visual Studio**, więc kompilacja przechodzi, a linkowanie kończy się na
`LNK1104: nie można otworzyć pliku "kernel32.lib"` — brakuje `LIB` ze ścieżkami do bibliotek SDK.
Przy własnym profilu CLion to środowisko ustawia i wszystko działa.

1. **Settings → Build, Execution, Deployment → CMake**: zostaw profil `Debug`, wyłącz profile
   pochodzące z presetów.
2. Łańcuch narzędzi: **Visual Studio**, architektura **amd64**. Generator: **Ninja** (CLion nie
   obsługuje generatora Visual Studio).
3. Nic więcej nie trzeba ustawiać: łańcuch narzędzi vcpkg podstawia `CMakeLists.txt` samodzielnie
   z `VCPKG_ROOT`, a wszystkie flagi — standard, ostrzeżenia jako błędy, `/fp:strict` — i tak są
   w `CMakeLists.txt`, nie w presecie. Profil CLion-a i preset dają więc ten sam wynik.
4. Konfiguracja uruchomienia „gameserver (dymny, 50 tików)" jest wersjonowana w `gameserver/.run/`
   i pojawia się od razu.

`VCPKG_ROOT` musi być widoczne dla CLion-a — jeśli zmienna została ustawiona po jego uruchomieniu,
trzeba go zrestartować. Gdy jej nie ma, konfiguracja kończy się komunikatem mówiącym wprost, czego
brakuje. Budowa kompilatorem innym niż MSVC też zatrzymuje konfigurację komunikatem
z `CMakeLists.txt` — cicha budowa MinGW obok reszty projektu byłaby gorsza niż błąd.

Po każdej nieudanej próbie: **Reset Cache and Reload Project**. Zepsuty cache zostaje w katalogu
budowania i samo przeładowanie go nie naprawi.

> **Jeśli mimo wszystko chcesz w CLion profil z presetu** — uruchom CLion z Developer PowerShella.
> Wtedy dziedziczy gotowe środowisko Visual Studio i preset `windows-ninja` konfiguruje się
> poprawnie. Zysk jest jednak żaden: preset wnosi tylko generator, typ budowy i łańcuch narzędzi
> vcpkg, a te CLion i tak ustawia sam.

> **Jedno tarcie:** `proto/game.proto` leży poza projektem CMake (bo należy też do klienta), więc
> w drzewie CLion-a go nie widać. Do edycji: **File → Attach Directory to Project** na katalogu
> `proto/`, albo otwarcie całego repozytorium i „Load CMake Project" na `gameserver/CMakeLists.txt`.

Preset `linux-clang` buduje to samo Ninją i klangiem; jest utrzymywany od pierwszego dnia, bo
produkcja stoi na Linuksie (plan serwera gry, decyzja 6.5).

---

## 9. Czego jeszcze nie ma

Uporządkowane od najbliższego do najdalszego.

| # | Brak | Uwagi |
|---|---|---|
| 1 | **Serwer gry** | szkielet (E1) stoi; brakuje gniazda, świata i mapy — etapy E2–E5 z [plan-serwera-gry.md](plan-serwera-gry.md). Sama symulacja to osobny plan |
| 2 | **Podpis biletu** | dziś nieprzezroczysty ciąg; krzywa wybrana (ECDSA P-256), zostaje implementacja |
| 3 | **Prawdziwa alokacja** | `LocalProcessMatchAllocator`, potem agent na maszynie; dziś atrapa |
| 4 | Odbiór wyniku meczu | `Internal/` na mTLS, idempotentny zapis po `matchId`; stan `Completed` czeka gotowy |
| 5 | Katalog map | jedna pozycja na sztywno (`moon`, 100 aktorów); docelowo tabela z §4.2 |
| 6 | Konta i logowanie | dziś wyłącznie goście; `Player` jest przygotowany na dowiązanie konta |
| 7 | PostgreSQL | dziś SQLite; wymiana dotyczy jednej linii w `AddInfrastructure` i migracji |
| 8 | Testy API end-to-end | `WebApplicationFactory` podłączona, ale nieużywana; patrz §6.2 |
| 9 | Testy frontendu | patrz §6.3 |
| 10 | Widoki `guide` i `contact` | zaślepki |
| 11 | Generowanie klientów HTTP z OpenAPI | typy TS pisane ręcznie |
| 12 | Skalowanie poziome | stan lobby w pamięci jednej instancji; wymaga backplane, lidera zegara i innego zamiatania meczów |

Plan wprowadzenia punktów 1–4 opisuje [plan-alokacji-meczu.md](plan-alokacji-meczu.md); etap 1
i większość etapu 2 są już zrobione (§4.9) — zostaje sam podpis biletu.

Punkt 1 ma własny dokument: [plan-serwera-gry.md](plan-serwera-gry.md) opisuje szkielet — proces
w C++, protokół, weryfikację biletu i mapę na ekranie gracza, **bez symulacji**. Podpis biletu
(punkt 2) wchodzi razem z nim, bo dopiero game-serwer ma go czym weryfikować.
