# Plan: szkielet serwera gry

Od pustego katalogu do gracza, który wchodzi do meczu i widzi mapę.

**Status:** zatwierdzony 30.07.2026 — decyzje z §6 rozstrzygnięte, poza 6.8
**Zakres:** luka nr 1 z [dokumentacja-aplikacji.md](dokumentacja-aplikacji.md) §9 oraz domknięcie etapu 2
z [plan-alokacji-meczu.md](plan-alokacji-meczu.md) — podpis biletu
**Podstawa:** [architektura-gry-terytorialnej.md](architektura-gry-terytorialnej.md) — §4.1, §4.3, §5④⑤,
§6, §8, D2, D4, D7, D8, D9, D10, D12, D13, D14

---

## 1. Punkt wyjścia

Meta doprowadza gracza do drzwi: zamraża roster, zakłada mecz, przydziela sloty, wydaje bilety
i nawiguje klienta na `/match/{matchId}`. Pod adresem z biletu **nie ma nikogo** — alokator jest
atrapą, a bilet jest nieprzezroczystym ciągiem, którego nikt nie weryfikuje.

Ten plan buduje drugą stronę tych drzwi.

**Kryterium ukończenia: gracz wchodzi do meczu i widzi mapę.** Nie „serwer wysyła keyframe" —
mapa ma być na ekranie, bo dopiero to dowodzi, że łańcuch `.tmap` → keyframe → protobuf → worker →
kanwa nigdzie się nie łamie. Wysłany, ale nieodczytany keyframe potrafi być błędny przez miesiące.

| Działa | Nie istnieje |
|---|---|
| Wejście do meczu biletem, wyjście, powrót po zerwaniu połączenia (D14) | ekspansja, ekonomia, miasta |
| Statyczna mapa: teren i woda, kamera, pan i zoom | jakakolwiek komenda gracza |
| Lista slotów z nickami i kolorami, ranking na zerze | boty podejmujące decyzje — sloty i nicki są, ruchów nie |
| Tykający zegar meczu i puste snapshoty 5 Hz | wynik meczu, replay |

Granica jest ostra celowo: **cokolwiek dotyka tablicy `owner` inaczej niż przez wczytanie terenu,
jest już symulacją** i należy do następnego planu. Puste snapshoty nie są atrapą — to ta sama
ścieżka, którą pojadą delty, przećwiczona zanim będzie co przez nią wysyłać.

---

## 2. Przepływ docelowy

```
① meta: MatchLauncher → IMatchAllocator.AllocateAsync
                        + manifest (sloty, nicki, kolory RGB, ziarno)       ← §3.5

② orkiestrator uruchamia proces
   gameserver --match-id {id} --port {p} --map moon.tmap --seed {s}
              --max-actors 100 --ticket-key ticket.pub --manifest -

③ proces wstaje
   wczytuje teren → owner[] (woda = 255) → sloty → nasłuch ws://127.0.0.1:{p}
   NIE nasłuchuje na 0.0.0.0 — przed nim stoi proxy z D9

④ klient łączy się przez proxy
   wss://gs.example.com/match/{matchId}  →  ws://127.0.0.1:{p}/match/{matchId}
   C→S  ClientHello { ticket }

⑤ proces weryfikuje bilet OFFLINE
   podpis ES256 kluczem publicznym → exp → matchId == własny → slot wolny → nonce nieużyty
   S→C  MatchInit { mapId, mapSha256, tickRate, yourSlot, slots[], seed }
   S→C  Snapshot  { is_keyframe = true, runs[...] }

⑥ klient dociąga teren i rysuje
   GET /maps/{mapId}/{sha256}/terrain.bin        ← D13, cache immutable, w dev przez proxy
   worker: terrain + owner z keyframe'a → pixels → OffscreenCanvas 2000×1000
   główny wątek: rAF, jeden drawImage z transformacją kamery

⑦ pętla
   sim 10 Hz (na razie tylko licznik ticków), send 5 Hz, PublicState co 5. snapshot
   ping ramką WS co 5 s → darmowa detekcja martwych połączeń

⑧ proces gasi się sam
   nikt nie przyszedł przez 120 s  albo  ostatni gracz wyszedł 120 s temu
```

---

## 3. Co dochodzi w kodzie

### 3.1 Wspólny schemat — `proto/` na poziomie repozytorium

`.proto` **nie należy do game-serwera**, tylko do kontraktu między nim a klientem. Trzymany
w `gameserver/proto/` byłby własnością jednej strony, a druga sięgałaby po niego przez `../`.

```
proto/game.proto          jedno źródło prawdy schematu (D2)
```

Kod generowany **nie wchodzi do repozytorium** — ani po stronie C++, ani TS. Po stronie C++ robi to
`protobuf_generate` w CMake, po stronie klienta skrypt npm z `@bufbuild/protoc-gen-es`. Powód jest
prozaiczny: wygenerowany plik w repo prędzej czy później rozjedzie się ze schematem i nikt tego nie
zauważy, bo diff jest nieczytelny.

Schemat z §6 dokumentu architektury wchodzi w całości, także wiadomości, których szkielet nie
wypełnia (`Command`, `AttackOrder`, `MatchEnd`). Pola są tańsze teraz niż migracja potem.

> **Jedno odstępstwo od §6:** `MyState` nie ma miejsca w `ServerMsg`. Skoro ma iść osobną
> wiadomością (§6, „łamie wspólny bufor"), musi być wariantem `oneof` — inaczej nie da się jej
> wysłać. Dopisujemy `MyState my_state = 6;`.

### 3.2 Drzewo i budowanie

```
gameserver/
├── CMakeLists.txt
├── CMakePresets.json          windows-msvc (terminal, CI), windows-ninja, linux-gcc
├── .run/                      wersjonowana konfiguracja uruchomienia dla CLion
├── vcpkg.json                 manifest: boost-asio, boost-beast, protobuf, gtest
│                              (openssl dochodzi w E2, xxhash w E3 — wtedy, gdy są używane)
├── src/                       drzewo z §4.3 dokumentu architektury
├── tools/tmapgen/             generator pliku mapy (§3.6)
└── tests/                     gtest
```

| Narzędzie | Stan na tej maszynie |
|---|---|
| MSVC 2022, CMake 4.3.2, Ninja | są |
| vcpkg | jest (`VCPKG_ROOT=C:\dev\vcpkg`), tryb manifestowy, `installed/` powstanie przy budowie |
| Conan 2.28 | jest, ale wtedy w pętli budowania siedzi drugie narzędzie i Python |
| protoc, boost | dojdą z vcpkg |

**Rekomendacja: vcpkg w trybie manifestowym.** `VCPKG_ROOT` jest już ustawione, integracja z CMake
to jedna linia `CMAKE_TOOLCHAIN_FILE` w presecie, a lista zależności leży w repozytorium obok kodu.

> **Pierwsza budowa potrwa.** Boost i protobuf ze źródeł to realnie 20–40 minut. To jednorazowe
> (cache w `%LOCALAPPDATA%\vcpkg`), ale warto o tym wiedzieć, zanim się uzna, że coś zawisło.

Do `.gitignore` dochodzi `gameserver/build/`, `vcpkg_installed/` i `*.tmap` — plik mapy waży 2 MB
i jest generowany, więc nie ma czego wersjonować. Do `.editorconfig` dochodzi sekcja
`[*.{cpp,hpp,h}]`; reszta pliku dotyczy C# i zostaje nietknięta.

### 3.3 Sieć — Beast, jeden wątek, zero mutexów

Model z D8 bez odstępstw: jeden `io_context`, jeden wątek, `steady_timer` napędza tick. Przy
procesie na mecz (D7) współbieżność nie ma czego przyspieszyć, a wprowadza całą klasę błędów.

**Asynchroniczność przez korutyny, nie przez uchwyty** (decyzja 6.10). Sesja WebSocketa to
z natury sekwencja: przyjmij upgrade → przeczytaj `ClientHello` → zweryfikuj bilet → wyślij
`MatchInit` → wyślij keyframe → czytaj komendy do końca meczu. Zapisana uchwytami rozpada się na
`on_accept`, `on_read`, `on_write` powiązane wyłącznie kolejnością wywołań, a stan sesji trzeba
przenosić przez `shared_from_this`. Jako korutyna zostaje jedną funkcją czytaną z góry na dół,
a ramka korutyny **jest** stanem sesji.

**Acceptor stoi na `127.0.0.1`, nie na `0.0.0.0`.** Proces mówi gołym `ws://`, bo TLS terminuje
proxy (D9) — wystawiony na świat byłby nieszyfrowanym wejściem do meczu. To jedna linia i jedno
z tych miejsc, gdzie domyślna wartość jest niebezpieczna.

Sesja:

| Element | Zachowanie |
|---|---|
| Ścieżka upgrade'u | `/match/{matchId}` — `matchId` musi zgadzać się z własnym, inaczej 404 przed upgrade'em |
| Ramki | wyłącznie binarne, `binary(true)` |
| Kolejka wyjściowa | jeden zapis naraz; kolejne czekają w `deque` |
| Backpressure | powyżej 256 KB w kolejce **rozłącz** (D4) — przy ~1 KB na snapshot rosnący bufor znaczy, że klient i tak nie żyje |
| Ping | ramka WS co 5 s; przeglądarka odpowiada automatycznie, więc detekcja zerwania jest darmowa (§5⑤) |

Broadcast to jeden preserializowany bufor `ServerMsg` (§6, „trik z prekodowaniem") trzymany
w `shared_ptr<const std::string>` — każda sesja dostaje ten sam wskaźnik, nie kopię.

### 3.4 Bilet — podpis i weryfikacja offline

Etap 2 planu alokacji zostawił jedno zdanie do dopisania i to jest ten moment: bez weryfikacji
szkielet wpuszcza każdego, kto zna `matchId`, a `matchId` jest w adresie.

**Meta.** `MatchTicketService` zamienia zawartość, nie kontrakt — `Issue` nadal zwraca
`MatchTicket(Value, ExpiresAt)`, więc `MatchLauncher`, `MatchesController` i cały klient zostają
nietknięte. W środku JWT ES256 z claimami z §5③: `playerId`, `matchId`, `slot`, `nonce`, `exp`.
Klucz prywatny (PEM PKCS#8) w user-secrets, tak samo jak `Jwt:SigningKey`.

**Game-serwer.** `meta/ticket_verifier.cpp`, klucz publiczny z pliku wskazanego przez `--ticket-key`.
Żadnego HTTP do meta — restart ASP.NET nie ma prawa zerwać trwających meczów (§4.3).

> **Pułapka formatu podpisu — i drobna korekta do dokumentu architektury.**
> W JWS podpis ES256 to **surowe `R‖S`, 64 bajty** (RFC 7518), a nie DER. Po stronie .NET wychodzi
> to samo z siebie: `ECDsa.SignData` domyślnie zwraca IEEE P-1363, czyli dokładnie `R‖S`.
> Ale **OpenSSL oczekuje DER** — `EVP_DigestVerify` dostanie 64 bajty i po prostu odmówi. W C++
> trzeba złożyć `ECDSA_SIG` z dwóch połówek i przepuścić przez `i2d_ECDSA_SIG`. Dwadzieścia linii,
> pod warunkiem że się o nich wie; bez tej wiedzy to jest wieczór zgadywania.
>
> Przy okazji: §5③ dokumentu wycenia koszt ECDSA na „~70 B DER zamiast 64 B". Przy JWT ten koszt
> **nie istnieje** — podpis ma 64 bajty tak samo jak Ed25519, a cały narzut biletu to base64 JSON-a
> (~350 znaków razem). Argument z §6.1 planu alokacji był ostrożniejszy, niż musiał być; decyzja
> zostaje ta sama, uzasadnienie robi się mocniejsze. Dokument architektury zasługuje na dopisek.

Weryfikacja po kolei, każdy krok zamyka inną drogę:

| Sprawdzenie | Co bez niego przechodzi |
|---|---|
| Podpis | bilet wypisany przez kogokolwiek |
| `exp` | bilet sprzed tygodnia |
| `matchId` == własny | bilet do innego meczu na tej samej maszynie |
| `slot` w `1..maxActors` | slot spoza tablicy — natychmiastowy UB |
| `nonce` nieużyty | to samo poświadczenie użyte dwa razy |

Zbiór zużytych `nonce` to zwykły `unordered_set` w procesie. Jednorazowość rozwiązuje się sama
dzięki D7 i **akurat tutaj kontener bez porządku jest w porządku** — warstwa sieciowa nie wchodzi
do hasha stanu świata, więc §8 jej nie dotyczy.

### 3.5 Manifest — luka, którą trzeba nazwać

`MatchInit` niesie `slots[]` z nickami i kolorami. **Dziś nie ma nimi jak dojść do procesu.**
`MatchAllocationRequest` świadomie nie zawiera rostera („im mniej orkiestrator wie, tym mniej trzeba
w nim zmieniać"), a plan alokacji §3.1 wspomina o manifeście, którego nikt nie zbudował.

Trzy warianty i dlaczego dwa odpadają:

| Wariant | Ocena |
|---|---|
| Proces dopytuje meta przy starcie | wprowadza zależność od meta w ścieżce startu meczu — dokładnie to, czego §4.3 zabrania |
| Sloty budowane z biletów przy podłączaniu | pierwszy gracz dostaje `MatchInit` z jednym slotem; nie ma skąd wziąć botów |
| **Manifest podany procesowi przy starcie** | orkiestrator go tylko przekazuje, nie rozumie i nie przechowuje |

**Rekomendacja: manifest przez stdin procesu** (`--manifest -`), a nie przez argumenty ani plik.
Nicki graczy trafiłyby do listy procesów całej maszyny, a plik trzeba by sprzątać. Model
orkiestratora zostaje nietknięty: `MatchAllocationRequest` dostaje pole `Manifest` typu
nieprzezroczystego dla wszystkiego po drodze.

Dwie rzeczy przy okazji:

- **Konwersja HSV → RGB robi meta.** Domena trzyma HSV, protokół chce `color_rgb` (§6). Funkcji nie
  ma jeszcze nigdzie — dochodzi w warstwie aplikacji, żeby C++ nigdy nie dowiedział się o istnieniu
  HSV.
- **Boty nie są w manifeście.** Nie mają wiersza w bazie i nie muszą mieć: ich nicki i kolory
  generuje game-serwer deterministycznie z ziarna meczu. To nie jest oszczędność, tylko warunek
  z §8 — boty są odtwarzane przez re-symulację, więc wszystko, co ich dotyczy, musi wynikać
  z ziarna.

### 3.6 Świat, mapa i keyframe

**Pliku mapy nie ma.** Format `.tmap` z D13 jest opisany, ale nikt nie wygenerował ani jednego
bajtu, a bez terenu nie ma keyframe'a, czyli nie ma czego udowodnić.

`tools/tmapgen` — narzędzie w tym samym projekcie CMake, dzielące nagłówek z czytnikiem. Generuje
`moon.tmap` 2000×1000 deterministycznie z podanego ziarna. Wersja w C# byłaby wygodniejsza (dotnet
już jest w pętli), ale wtedy format ma dwie niezależne implementacje i rozjedzie się przy pierwszej
zmianie nagłówka.

```
world.cpp     terrain (do wczytania) + owner[] (2 MB, u8) + slots[]
              woda z terenu wchodzi do owner[] jako 255 (D12)
keyframe.cpp  RLE row-major → OwnershipRun[]
```

Ładna właściwość: **keyframe szkieletu nie jest pusty.** Woda daje realne runy, więc pierwsza
wiadomość ma kilkadziesiąt kilobajtów i mierzy się w niej dokładnie to, co §7 przewiduje.

> **`mmap` — nie teraz.** D13 zakłada `mmap` read-only i współdzielenie jednego mapowania przez
> wszystkie procesy na maszynie. To POSIX; na Windowsie odpowiednikiem jest `CreateFileMapping`,
> a **zysk istnieje wyłącznie tam, gdzie na jednym hoście stoi 250 procesów** — czyli na produkcji,
> nie w dev. W szkielecie: interfejs `TerrainSource` i zwykły odczyt 2 MB. Mapowanie wchodzi razem
> z agentem, gdy zaczyna cokolwiek znaczyć.

**`mapSha256` liczy proces, nie meta.** Sensem tego pola (D13) jest wykrycie, że klient ma w cache'u
inny teren niż serwer. Wartość przepisana z bazy poświadczałaby to, co meta **myśli** o pliku;
policzona z faktycznie wczytanych bajtów poświadcza teren, na którym mecz naprawdę się toczy.
Pole w `MapDefinition` dochodzi dopiero z CDN-em, czyli w etapie 3 planu alokacji.

**Skąd klient bierze ten sam plik.** Docelowo z CDN-u pod ścieżką adresowaną hashem (D13).
W dev CDN-u nie ma, więc plik serwuje meta spod `/maps/{mapId}/{sha256}/terrain.bin` — **z segmentem
hasha ignorowanym przy szukaniu pliku na dysku**. To świadome uproszczenie: w dev istnieje jeden
plik na mapę, a hash w ścieżce pełni tam wyłącznie rolę klucza cache'a przeglądarki, dokładnie tak
jak na produkcji. Nagłówki (`immutable`, rok życia) zostają te same, bo to one są testowane.

Kompresji w dev nie ma — 2 MB z `localhost` schodzi w kilkanaście milisekund, a `Content-Encoding`
na plikach statycznych to konfiguracja ingressu, nie aplikacji.

### 3.7 Pętla ticków i gaszenie procesu

Stały krok z akumulatorem, sim 10 Hz, send co drugi tick (D3). W szkielecie sim wyłącznie zwiększa
licznik — **i nie rusza RNG**, bo pobrana i wyrzucona liczba to pierwszy sposób, w jaki determinizm
umiera po cichu.

Gaszenie procesu jest częścią szkieletu, nie dodatkiem na później:

| Warunek | Reakcja |
|---|---|
| Nikt nie połączył się przez 120 s od startu | koniec procesu, kod wyjścia ≠ 0 |
| Ostatni gracz rozłączony 120 s temu | koniec procesu, kod 0 (D14 punkt 2 — tyle trwa okno reconnectu) |
| Twardy limit czasu meczu (30 min) | koniec procesu; D7 obiecuje ograniczony czas życia i ktoś musi tej obietnicy pilnować |

Bez tego pierwszy dzień z `LocalProcessMatchAllocator` zostawia na maszynie proces na każde lobby,
a lobby otwiera się co kilka minut w nieskończoność.

### 3.8 Determinizm — zabezpieczenie przed pierwszą linią symulacji

§8 mówi to wprost: determinizm gnije cicho i odkrywa się to w ósmym miesiącu. Szkielet nie ma jeszcze
czego desynchronizować i **dlatego** jest jedynym momentem, w którym zabezpieczenie jest darmowe.

- `sim/rng.cpp` — PCG seedowany ziarnem meczu; jedyne źródło losowości w procesie
- `replay/hasher.cpp` — xxHash po `owner[]`, stanie RNG i stanie aktorów, co N ticków
- `/fp:strict` (MSVC) i `-ffp-contract=off` (Clang) w presetach; `float` w `sim/` nie ma prawa wystąpić
- test w `tests/` sprawdzający, że dwa przebiegi z tym samym ziarnem dają identyczny łańcuch hashy —
  na pustej symulacji jest trywialny i to jest zaleta: **przestanie być trywialny dokładnie wtedy,
  gdy zacznie coś znaczyć**

Repozytorium nie ma dziś CI. Test determinizmu jest pierwszym, który realnie go wymaga — bez
automatycznego przebiegu zabezpieczenie z §8 sprowadza się do dobrych chęci.

### 3.9 Meta — co się zmienia

| Miejsce | Zmiana |
|---|---|
| `MatchTicketService` | ES256 zamiast nieprzezroczystego ciągu; kontrakt bez zmian |
| `MatchOptions` | `TicketPrivateKeyPem` (user-secrets), `TicketPublicKeyPath` do wypisania przy starcie w dev |
| `MatchAllocationRequest` | pole `Manifest` |
| Warstwa aplikacji | budowa manifestu + konwersja HSV → RGB |
| `LocalProcessMatchAllocator` | uruchamia binarkę, wybiera wolny port, czeka na gotowość (etap E4) |
| `Match:MatchWebSocketBaseUrl` | w dev `wss://localhost:4200/match` — patrz §3.11 |
| Serwowanie `/maps/...` | pliki statyczne z nagłówkami z D13, wyłącznie na czas braku CDN-u (§3.6) |

**Bilet zostaje dla klienta nieprzezroczystym ciągiem** i po podpisaniu nic się w tym nie zmienia —
`MatchGateway` przechowuje go i odnawia dokładnie tak jak dziś. Cała reszta zmian po stronie klienta
to nowy kod, nie przeróbki istniejącego.

### 3.10 Klient — mapa na ekranie

Zaślepka w `features/match` zamienia się w realny widok. Podział z §4.1 obowiązuje od pierwszej
linii, bo **przeniesienie renderowania do workera po fakcie jest przepisaniem, nie refaktorem**.

```
features/match/
├── net/game-socket.worker.ts    WS + protobuf + owner[] + pixels        ⟵ WORKER
├── render/map-renderer.ts       OffscreenCanvas, chunki, ImageData      ⟵ WORKER
├── render/camera.ts             pan i zoom, transformacja ekran↔kafelek
└── match.ts                     sygnały wyłącznie dla UI
```

Główny wątek oddaje kanwę przez `transferControlToOffscreen()` i **nigdy** nie dotyka 12 MB typed
arrays. Do Angulara wraca kilkaset bajtów stanu UI. Aplikacja jest zoneless, ale to nie zwalnia
z tego podziału — pętla renderowania ma zostać poza cyklem Angulara niezależnie od trybu.

| Rzecz | W szkielecie |
|---|---|
| Dwie pętle (sieciowa 5 Hz / rAF 60 Hz) | **tak** — to jest ta decyzja, której nie da się dopiąć później |
| `OffscreenCanvas` 2000×1000 + `drawImage` z kamerą | **tak** |
| `imageSmoothingEnabled = false` | **tak** — bez tego zoom daje rozmytą papkę zamiast pikseli |
| Śledzenie brudnych chunków 128×128 | **nie** — bez delt nie ma czego brudzić; wchodzi razem z nimi |
| Paleta slotów z `MatchInit.slots[]` | **tak**, choć w szkielecie maluje wyłącznie wodę i pustkowie |
| Porównanie `mapSha256` z cache'em | **tak** — to jedyny moment, w którym D13 daje się przetestować |

Reconnect po stronie klienta jest **funkcjonalnością szkieletu**, nie dodatkiem: zerwane połączenie
→ `ensureTicket` (już istnieje) → nowy WS → keyframe → dalej ta sama mapa. Ścieżka po stronie meta
działa od etapu 2 planu alokacji i przez cały ten plan nie jest ruszana; dochodzi wyłącznie
podłączenie jej do gniazda.

> **Pułapka: zakładka w tle.** Przeglądarka dławi `requestAnimationFrame` w niewidocznej karcie
> praktycznie do zera, ale **worker i WebSocket działają dalej**. To akurat układ, który nam
> sprzyja: stan przychodzi na bieżąco, a rysowanie wraca przy pierwszej klatce po powrocie. Warunek
> jest jeden — pętla sieciowa nie może być napędzana przez `rAF`. Stąd rozdzielenie z §4.1 ma
> konsekwencję poprawnościową, nie tylko wydajnościową.

### 3.11 Dev — pułapka, która zatrzyma pierwszą próbę

Klient deweloperski chodzi na **`https://localhost:4200`**. Przeglądarka **nie otworzy `ws://` ze
strony podanej po https** — to mixed content i blokada jest twarda, bez obejścia po stronie kodu.
A game-serwer z D9 mówi gołym `ws://`, bo TLS ma terminować proxy.

Rozwiązanie jest tym, czym i tak jest produkcja: **proxy**. Dev-server Angulara umie to zrobić sam.

```jsonc
// client/proxy.conf.json — podpięty w angular.json jako "proxyConfig" w opcjach serve
{
  "/match": { "target": "ws://127.0.0.1:5101", "ws": true },
  "/maps":  { "target": "https://localhost:5001", "secure": false }
}
```

Klient łączy się z `wss://localhost:4200/match/{id}` — ten sam origin, żadnego mixed content,
żadnego CORS-u — a dev-server rozmawia z procesem po gołym `ws`. To jest dokładnie rola Envoya
z D9, więc ścieżka dev i ścieżka produkcyjna różnią się wyłącznie tym, kto stoi pośrodku. Tym samym
wpisem idzie `/maps` do meta, żeby teren przychodził spod tego samego origin co reszta — na
produkcji stoi tam CDN i klient nie zauważa różnicy.

**Konsekwencja:** w dev port jest stały (5101), bo cel proxy jest wpisany w plik. To wystarcza,
dopóki na maszynie stoi jeden mecz naraz — czyli przez cały szkielet.

---

## 4. Macierz awarii

| Awaria | Zachowanie | Kto to widzi |
|---|---|---|
| Bilet bez podpisu / zły podpis / wygasły | close 1008, bez szczegółów w powodzie | klient: „bilet odrzucony", ścieżka ponownego wydania już istnieje |
| `matchId` w ścieżce ≠ `matchId` procesu | 404 przed upgrade'em | nikt — to skanowanie, nie gracz |
| Ten sam slot łączy się drugi raz | stare połączenie rozłączone, nowe przyjęte | nikt; **to jest reconnect z D14** i działa za darmo |
| Powtórzony `nonce` | odrzucenie | gracz z podwójną kartą — dostanie świeży bilet |
| Klient nie nadąża (>256 KB w kolejce) | rozłączenie (D4) | gracz: zerwane połączenie, wraca biletem |
| Brak pliku mapy / zły nagłówek | proces nie wstaje, kod ≠ 0 | alokacja się nie udaje → `MatchStartFailed` → nowe lobby (**już zaimplementowane**) |
| Proces padł w trakcie meczu | mecz zostaje `Live` w meta; nikt tego nie sprząta | gracze — do etapu 4 planu alokacji (odbiór wyniku) nie ma tego jak zamknąć |
| Proces gaśnie z braku graczy | kod wyjścia mówi, czy to porzucenie, czy normalny koniec | logi orkiestratora |
| `mapSha256` z `MatchInit` ≠ hash terenu w cache'u | klient dociąga plik spod nowej ścieżki | nikt — **to jest cel tego pola**, nie awaria |
| Teren nie doszedł (404, zerwane pobranie) | widok meczu mówi wprost i pozwala wrócić; bilet zostaje ważny | gracz |
| Przeglądarka bez `OffscreenCanvas` | komunikat zamiast cichego czarnego ekranu | gracz na bardzo starej przeglądarce |

Odzyskiwanie po crashu z D10② (re-symulacja z logu komend) **nie jest częścią szkieletu** — nie ma
jeszcze ani komend, ani czego odtwarzać.

---

## 5. Kolejność wprowadzania

**E1 — fundament. ✅ ZROBIONY (30.07.2026).** `gameserver/` z CMake, vcpkg i presetami;
`proto/game.proto`; codegen po obu stronach; `main` parsujący argumenty i tykający pustą pętlą;
21 testów; pipeline CI na obu platformach.
*Dowód:* zegar przez 110 tików odszedł od czasu rzeczywistego o kilkanaście milisekund i nie zgubił
ani jednego tiku.

> **Trzy rzeczy wyszły inaczej, niż zapowiadał plan.** Preset windowsowy dla terminala i CI używa
> generatora Visual Studio zamiast Ninji — Ninja wymaga środowiska `vcvars`, czyli Developer
> PowerShella przy każdym uruchomieniu, i ten koszt płaciłoby się codziennie. Obok stoi preset
> ninjowy, bo CLion generatora Visual Studio nie obsługuje w ogóle. Doszła opcja `--max-ticks`, żeby proces dało
> się sprawdzić bez zabijania go z zewnątrz; bez niej „binarka startuje" nie jest asercją, którą CI
> może wykonać. Boost przy pierwszej kompilacji zażądał `_WIN32_WINNT` — bez tego Asio zakłada
> Windows 7 i wyłącza część mechanizmów, których docelowo używa.

**E2 — sieć i bilet. ✅ ZROBIONY (30.07.2026).** Beast, sesja, backpressure, ping/pong. ES256 po
stronie meta i weryfikacja offline po stronie C++. Klient testowy w `client/tools/`.
*Dowód:* klient w TypeScripcie wszedł biletem podpisanym przez .NET i odebrał 94 snapshoty w rytmie
5 Hz przy RTT poniżej milisekundy; bilet podpisany obcym kluczem dostaje zamknięcie 1008.
**Domyka etap 2 planu alokacji.**

> **Dwa błędy, których nie znalazłyby testy jednostkowe.** Pierwszy: termin ustawiony na czas
> handshake'u zostawał w mocy po przejściu na WebSocket, więc **każde połączenie ginęło po dziesięciu
> sekundach** — a testy trwają milisekundy i widziały wyłącznie sukces. Drugi: akceptor trzymał
> `io_context` przy życiu po końcu meczu, więc proces nie kończył pracy i przyjmował graczy do
> meczu, którego już nie było. Oba wyszły przy pierwszym uruchomieniu całej ścieżki naraz i oba są
> argumentem za tym, żeby klient testowy powstał w tym samym etapie co serwer, a nie „kiedyś potem".
>
> Klient testowy stoi w `client/tools/`, a nie w `gameserver/tools/` jak zapowiadał plan: używa
> codegenu i `node_modules` klienta, więc osobny projekt npm byłby drugą kopią tego samego.

**E3 — świat i keyframe.** `tmapgen`, wczytanie terenu, `owner[]`, sloty z manifestu, boty z ziarna,
`MatchInit` + keyframe RLE, `PublicState` co 1 Hz, gaszenie procesu.
*Dowód:* testowy klient dostaje keyframe i liczy runy; rozmiar zgadza się z §7 (60–80 KB).

**E4 — spięcie z meta.** `LocalProcessMatchAllocator`, `proxy.conf.json`, manifest przez stdin,
serwowanie `/maps` w dev.
*Dowód:* odliczanie lobby dobija do zera i **prawdziwy proces** przyjmuje prawdziwą przeglądarkę.

**E5 — mapa na ekranie.** Worker, `OffscreenCanvas`, kamera, paleta slotów, sprawdzenie
`mapSha256`, reconnect podpięty do gniazda.
*Dowód:* **kryterium z §1** — gracz wchodzi z lobby, widzi mapę, wychodzi, wraca po F5 i widzi ją
znowu.

Symulacja (ekspansja, ekonomia, miasta, boty) to osobny plan po E5. Wchodzi w gotową ramę: delty
mają już czym jechać, kanwa ma już co odświeżać, a jedyne, co dojdzie po stronie klienta, to
śledzenie brudnych chunków.

---

## 6. Decyzje

Wszystkie poza 6.8 rozstrzygnięte **30.07.2026**, przed pierwszą linią kodu.

**6.1 Menedżer zależności. ✅ vcpkg w trybie manifestowym.** `VCPKG_ROOT` już wskazuje instalację,
integracja to jedna linia `CMAKE_TOOLCHAIN_FILE` w presecie, a lista zależności leży w repozytorium
obok kodu. Conan działa równie dobrze, ale dokłada Pythona do pętli budowania — a im mniej rzeczy
musi działać, żeby projekt się zbudował, tym lepiej.

**6.2 Skąd proces bierze roster. ✅ Manifest na stdin przy starcie** (§3.5). Wariant „proces pyta
meta" wprowadza zależność od meta w ścieżce startu meczu, czyli dokładnie to, czego zabrania §4.3.
Argumenty wiersza poleceń odpadają, bo nicki graczy trafiłyby do listy procesów całej maszyny.

**6.3 Kto generuje `.tmap`. ✅ Narzędzie C++ w tym samym projekcie**, dzielące nagłówek
z czytnikiem. Wersja w C# jest wygodniejsza dokładnie do chwili, w której format się zmieni —
a wtedy dwie niezależne implementacje rozjeżdżają się po cichu.

**6.4 Czy `mmap` teraz. ✅ Nie** — interfejs `TerrainSource` i zwykły odczyt. Jedyna korzyść
(współdzielona page cache) pojawia się przy wielu procesach na jednym hoście, czyli razem
z agentem. Do tego czasu byłby to kod platformowy pisany na zapas.

**6.5 Platformy budowania. ✅ Oba presety od pierwszego dnia** — `windows-msvc` i `linux-gcc` —
nawet jeśli linuksowego przez pierwsze tygodnie nikt nie odpali. Przenośność dopisana po roku
kosztuje wielokrotnie więcej niż utrzymywana od początku.

> **Korekta (31.07.2026): na Linuksie GCC, nie clang.** Pierwszy przebieg CI wywalił się na
> `std::expected`, którego „nie ma". Przyczyna nie jest tam, gdzie się jej szuka: clang 18 nie
> implementuje P0848 (warunkowo trywialne funkcje specjalne), więc definiuje
> `__cpp_concepts = 201907`, a libstdc++ chowa **cały nagłówek `<expected>`** za warunkiem
> `__cpp_concepts >= 202002`. Nie pomaga ani libstdc++ 14, ani nowszy clang. Z libc++ działa, ale
> wtedy pod libc++ musiałyby być zbudowane wszystkie zależności z vcpkg — czyli osobny triplet
> dla jednego nagłówka.
>
> GCC 13 kompiluje całość bez ostrzeżeń przy `-Wall -Wextra -Wpedantic`. Clang wraca do rozmowy,
> gdy nadrobi P0848 — do tego czasu byłby to kompilator, pod który trzeba pisać inaczej.

**6.6 Twardy limit czasu meczu. ✅ 30 minut i twarde wyjście.** D7 obiecuje ≤25 min i na tej
obietnicy stoi cała strategia deployu („przestań alokować i poczekaj"). Obietnica bez egzekwowania
jest tylko komentarzem.

**6.7 CI. ✅ Wchodzi z E1.** Test determinizmu (§3.8) jest pierwszym, który realnie go potrzebuje,
a dopóki budowa jest mała, konfiguracja jest krótka. Budowa C++ z vcpkg dopisana do gotowego
pipeline'u po roku to osobne przedsięwzięcie.

**6.8 Zachowanie terytorium rozłączonego gracza. ⏳ ZOSTAJE OTWARTE.** Kwestia z dokumentu
architektury (§10 pozycja 2, D14 punkt 3): zamarza czy przejmuje je bot. Tego planu **nie blokuje**
i to nie przypadek — bez symulacji rozłączony gracz nie ma terytorium, więc pytanie nie ma na czym
się zaczepić. Musi zapaść przed pierwszą linią symulacji, i to jako decyzja gameplayowa, nie
techniczna.

**6.9 Chunki 128×128 w kliencie. ✅ Nie teraz** (§3.10). Bez delt nie ma brudnych chunków, więc
byłaby to księgowość pilnująca pustego zbioru. Rozdzielenie dwóch pętli — sieciowej i `rAF` —
wchodzi natomiast **od razu**, bo to ono jest decyzją architektoniczną, a chunki tylko
optymalizacją w jej wnętrzu.

**6.10 Korutyny czy uchwyty w warstwie asynchronicznej. ✅ Korutyny** (`boost::asio::awaitable`,
`co_spawn`). Wyszło z pierwszej wersji zegara napisanej uchwytami: pętla oddawała sterowanie na
zewnątrz przez `std::function`, więc „koniec meczu" musiał wracać do zegara osobną ścieżką i być
sprawdzany w środku nadrabiania zaległości — na tyle nieoczywiste, że wymagało własnego testu.
W wersji korutynowej zegar **oddaje kolejny tik temu, kto o niego poprosi**, koniec meczu jest
zwykłym `break`, a pusty wynik `next()` jest jedynym sposobem, w jaki pętla się kończy.

Cena to narzut ramki korutyny na operację. Przy 64 połączeniach × 5 Hz jest bez znaczenia — ten
sam argument, którym dokument architektury uzasadnia wybór Beasta zamiast uWebSockets (D8). Model
wątkowy zostaje nietknięty: korutyny wznawiają się na tym samym executorze, więc jeden wątek i zero
mutexów obowiązują dalej.
