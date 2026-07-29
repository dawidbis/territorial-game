# Dokumentacja aplikacji

Opis **tego, co jest faktycznie zaimplementowane** w repozytorium — funkcjonalności, kontrakty,
konfiguracja, uruchomienie.

**Status:** v1 pre-alpha (meta-serwer + klient; brak serwera gry)
**Aktualność:** 30.07.2026

> **Relacja do drugiego dokumentu.** [architektura-gry-terytorialnej.md](architektura-gry-terytorialnej.md)
> opisuje **projekt docelowy** — symulację w C++, protobuf, delty kafelków, orkiestrację. Ten
> dokument opisuje **stan bieżący kodu**. Gdziekolwiek jest odwołanie w postaci „D11" albo „§5③",
> chodzi o decyzję albo sekcję z tamtego dokumentu.

---

## 1. Czym aplikacja jest dzisiaj

Działającą **poczekalnią przed meczem**: gracz wchodzi na stronę, dostaje tożsamość bez
rejestracji, ustawia nick i kolor, dołącza do lobby i widzi na żywo, kto jeszcze czeka.

Czego **nie ma**: samego meczu. Nie istnieje mapa, symulacja, ekspansja terytorium ani serwer gry —
lobby zbiera graczy i na tym się zatrzymuje. Jest to stan świadomy, nie niedokończony: odliczanie
domyślnie stoi, bo nie ma komu oddać meczu (patrz §5.8).

### 1.1 Mapa stanu implementacji

| Obszar | Stan |
|---|---|
| Tożsamość gościa, sesja, token | **gotowe** |
| Profil gracza (nick, kolor HSV) | **gotowe** |
| Lobby — roster, nagłówek, snapshoty na żywo | **gotowe** |
| Kanał realtime (SignalR) + zegar rozgłaszania | **gotowe** |
| Odliczanie + synchronizacja zegara klienta | **gotowe**, wyłączone przełącznikiem |
| Karencja rozłączenia, obsługa wielu kart | **gotowe** |
| Cykl życia lobby (zbieranie → start → zamknięcie) | **gotowe**, nieosiągalne przy wyłączonym odliczaniu |
| Persystencja gracza (EF Core + SQLite) | **gotowe** |
| Widoki `guide` i `contact` | **zaślepki** („in progres...") |
| Katalog map | jedna pozycja wpisana na sztywno |
| Alokacja meczu, bilety, game-serwer | **brak** — miejsce oznaczone logiem ostrzegawczym |
| Konta i logowanie | **brak** — tylko goście |
| Testy backendu (domena + warstwa aplikacji) | **30 testów, wszystkie zielone** |
| Testy integracyjne API | **puste rusztowanie** |
| Testy frontendu | **brak** (nietknięty szablon, patrz §8.3) |

---

## 2. Stack i struktura repozytorium

| Warstwa | Technologia |
|---|---|
| Klient | Angular 22 (zoneless, sygnały), TailwindCSS 4, DaisyUI 5, `@microsoft/signalr` 10 |
| Meta-serwer | .NET 10, ASP.NET Core (kontrolery + SignalR), EF Core 10 |
| Baza | **SQLite** (docelowo PostgreSQL) |
| Testy | xUnit, Shouldly, NSubstitute, `FakeTimeProvider` |

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
└── docs/
```

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

⑤ Wyjście
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

**Czego brakuje na końcu tego cyklu — i jak to jest zaznaczone.** Gdy lobby dobija do terminu
startu, `LobbyClock` loguje ostrzeżenie, że alokacja meczu nie jest zaimplementowana, i woła
`CloseAndReopen`. To jest dokładne miejsce, w które wejdzie wywołanie orkiestratora i rozesłanie
biletów (§5③ dokumentu architektury). Roster nowego lobby jest pusty; połączenia zostają, ale
członkostwo nie jest dziedziczone — klient zauważa zmianę `lobbyId` i dołącza ponownie, jeśli
gracz nadal siedzi na widoku lobby.

**Domyślnie ta ścieżka jest nieosiągalna.** `Lobby.CountdownEnabled` jest `false`, więc lobby
otwiera się przez `OpenFrozen`: licznik stoi na pełnym oknie, `StartsAt` jest `null`, `Starting`
nie da się osiągnąć. Pełna cykliczna logika jest zaimplementowana i przetestowana — uruchomienie
jej to **zmiana jednej wartości w konfiguracji**, bez dotykania kodu.

### 4.9 Nawigacja i spójność stanu w UI

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

### 4.10 Warstwa wizualna

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

### 4.11 Bezpieczeństwo

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

### 4.12 Persystencja

Jedna encja i jedna tabela. Migracja `20260728210541_InitialCreate`.

```
players
  id                TEXT  PK   (GUID, ValueGeneratedNever)
  nickname          TEXT       maxLength 20
  created_at        TEXT
  last_seen_at      TEXT
  color_hue         INTEGER
  color_saturation  INTEGER
  color_value       INTEGER
```

Dwa rozwiązania warte odnotowania:

- **`HsvColor` jako `ComplexProperty`**, nie owned entity: typy owned muszą być referencyjne,
  a `HsvColor` to `readonly record struct`. Mapuje się na trzy kolumny tej samej tabeli, bez
  sztucznej tożsamości.
- **`Nickname.FromTrusted` przy materializacji**, nie `Create`: walidacja należy do wejścia, nie do
  odczytu. Gdyby reguły nicka kiedyś się zaostrzyły, `Create` wysadzałby **czytanie** istniejących
  wierszy.

### 4.13 Diagnostyka

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

Samo podłączenie **nie** oznacza członkostwa: strona główna łączy się tylko po to, żeby widzieć
nagłówek na żywo.

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
```

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
```

`Roster()` zwraca listę w **stabilnej kolejności** — od najdawniej obecnego, z identyfikatorem jako
rozstrzygnięciem remisu. Sortowanie jest w domenie, a nie w warstwie prezentacji, bo to właściwość
lobby, a nie sposobu jego wyświetlenia; bez tego lista skakałaby graczom przed oczami, kolejność
słownika nie jest gwarantowana.

---

## 6. Testy

### 6.1 Backend — 30 testów, wszystkie zielone

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

### 6.2 Testy integracyjne API — rusztowanie

Projekt istnieje i jest podłączony (`InternalsVisibleTo` dla `WebApplicationFactory<Program>`),
ale zawiera wyłącznie pusty `UnitTest1`. **Brak pokrycia** dla: uwierzytelniania, ścieżek
kontrolerów, huba end-to-end i persystencji.

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
| `Lobby:CountdownEnabled` | `false` | **przełącznik uruchamiający cykl lobby** |
| `Lobby:DisconnectGraceSeconds` | `5` | karencja po utracie ostatniego połączenia |

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

---

## 9. Czego jeszcze nie ma

Uporządkowane od najbliższego do najdalszego.

| # | Brak | Uwagi |
|---|---|---|
| 1 | **Serwer gry** | cały mecz: symulacja, mapa, ekspansja, ekonomia. Dokument architektury, §4.3 |
| 2 | **Alokacja meczu i bilety** | miejsce oznaczone logiem w `LobbyClock`; §5③ |
| 3 | Włączenie odliczania | `Lobby:CountdownEnabled` — logika gotowa i przetestowana |
| 4 | Katalog map | jedna pozycja na sztywno (`moon`, 100 aktorów); docelowo tabela z §4.2 |
| 5 | Konta i logowanie | dziś wyłącznie goście; `Player` jest przygotowany na dowiązanie konta |
| 6 | PostgreSQL | dziś SQLite; wymiana dotyczy jednej linii w `AddInfrastructure` i migracji |
| 7 | Testy integracyjne API | rusztowanie bez testów |
| 8 | Testy frontendu | patrz §6.3 |
| 9 | Widoki `guide` i `contact` | zaślepki |
| 10 | Generowanie klientów HTTP z OpenAPI | typy TS pisane ręcznie |
| 11 | Skalowanie poziome | stan lobby w pamięci jednej instancji; wymaga backplane i lidera zegara |

Plan wprowadzenia punktów 1–3 opisuje [plan-alokacji-meczu.md](plan-alokacji-meczu.md).
