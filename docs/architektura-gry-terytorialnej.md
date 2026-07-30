# Architektura serwera gry terytorialnej

Dokument techniczny — podział warstw, rejestr decyzji, protokół sieciowy.

**Status:** draft v1
**Zakres:** wersja pierwotna gry (ekspansja terytorium, populacja, złoto, miasta)

---

## 1. Kontekst i zakres v1

Gra typu *territory expansion* na kafelkowej pikselowej mapie (mechanika zbliżona do openfront.io).

### Mechaniki w zakresie v1

1. **Rozszerzanie terytorium** — gracz wydaje globalną populację swojego terytorium na przejmowanie kafelków
2. **Zasoby** — populacja (rośnie funkcją zależną od wielkości terytorium i liczby miast), złoto (generowane pasywnie)
3. **Budynek** — miasto (kupowane za złoto, zwiększa przyrost populacji)

### Model ataku

- Atak jest **automatyczny i niekontrolowalny** po zainicjowaniu
- Gracz wybiera wyłącznie: **cel** (pustkowie / inny gracz / bot) oraz **% populacji** do zaangażowania
- Atak postępuje **po całej szerokości wspólnej granicy** — nie da się go zawęzić do fragmentu

### Parametry docelowe

| Parametr | Wartość |
|---|---|
| Rozmiar mapy | 2000 × 1000 = **2 mln kafelków** |
| Aktorzy w meczu | **100–254** (ludzie + boty) |
| Ludzcy gracze | 10–64 |
| Czas meczu | 10–25 min |
| Fog of war | **brak** — pełna widoczność mapy |
| Mapy | stałe, wczytywane z pliku |
| Boty | zawsze obecne |
| Predykcja klienta | **brak** — tylko efekty wizualne |
| Reconnect | patrz D14 (otwarte) |

---

## 2. Stack

| Warstwa | Technologia |
|---|---|
| Frontend | Angular 22, TailwindCSS, DaisyUI |
| Meta-serwer | ASP.NET Core Web API, EF Core, PostgreSQL |
| Game-serwer | C++23, Boost.Beast |
| Serializacja (mecz) | Protocol Buffers |
| Serializacja (meta) | JSON |
| Ingress | Envoy / nginx (terminacja TLS) |
| Assety statyczne | CDN |

---

## 3. Rejestr decyzji

Sekcja zawiera także **decyzje odrzucone** i **korekty** — one są tu najcenniejsze, bo bez nich za pół roku ktoś (być może ja sam) zapyta „a czemu nie zrobiliśmy tego prościej".

---

### D1 — Komendy przez WebSocket, nie REST

**Rozważane:** REST API na komendy gracza + WebSocket tylko na stan zwrotny.

**Decyzja:** jeden dwukierunkowy WebSocket. REST wyłącznie na meta (logowanie, lobby, statystyki, wyniki).

**Uzasadnienie — dlaczego REST na komendy odpada:**

- **Kolejność.** WebSocket to jeden uporządkowany strumień. REST przez pulę połączeń może dostarczyć żądania w innej kolejności niż wysłane. Wymagałoby to `seq` + bufora reorderingu, czyli i tak budowy kawałka protokołu strumieniowego.
- **Routing — problem główny.** WS trzyma sticky połączenie do konkretnego procesu symulującego mecz. REST wszedłby przez load balancer i trafił losowo. Wymagałoby to sticky routingu po `matchId` albo przerzucania komend szyną komunikatów do właściwej instancji.
- **CORS preflight.** `Content-Type: application/x-protobuf` **nie jest na liście CORS-safelisted**. Każdy POST cross-origin generuje dodatkowy `OPTIONS`. Klient jest w przeglądarce, więc to nie do obejścia poza `Access-Control-Max-Age` (Chrome i tak tnie do 2h).
- **Retry / idempotencja.** Proxy i klienci HTTP potrafią powtórzyć żądanie. Podwójny atak to błąd stanu gry. Wymagałoby kluczy idempotencji.
- **Auth.** WS uwierzytelnia się raz na handshake'u; REST walidowałby JWT przy każdej komendzie.
- **Narzut.** Ramka WS z protobufem ~10–30 B vs nagłówki HTTP (kilkadziesiąt–kilkaset B nawet z HPACK).

**Dodatkowy argument dla tej konkretnej gry:** komendy są **ekstremalnie rzadkie** (~0,1/s na gracza — atak co kilkanaście sekund, miasto co kilkadziesiąt). Cały ruch to snapshoty w dół. Utrzymywanie drugiego transportu dla tak nikłego strumienia nie ma żadnego uzasadnienia.

**Gdzie REST jest właściwy:** logowanie, lobby, matchmaking, katalog map, wyniki meczu, replaye, ranking. Czyli wszystko poza pętlą symulacji.

---

### D2 — Protocol Buffers

**Rozważane:** protobuf / FlatBuffers / własny bit-packing / JSON.

**Decyzja:** protobuf, z **kwantyzacją przed serializacją**.

**Uzasadnienie:**
- Codegen dla obu stron (C++ i TypeScript) z jednego pliku `.proto` — jedno źródło prawdy schematu
- Ewolucja schematu bez łamania klientów
- JSON odpada — narzut tekstowy przy 5 Hz × 2 mln-kafelkowej mapie jest nie do obrony

**Świadome ograniczenie:** protobuf nie pakuje bitowo. Rekompensujemy to kwantyzacją i kodowaniem delt (D5). Własny bit-packing dałby może kolejne 20–30%, ale kosztem utraty codegenu i ewolucji schematu — nieopłacalne przy naszym budżecie (patrz §7).

**Uwagi implementacyjne:**
- `optional` (proto3 ≥ 3.15) na polach delt — inaczej nie odróżnisz „0" od „niewysłane"
- `[packed = true]` na `repeated` skalarach — bez tego każdy element dostaje własny tag
- C++: `google::protobuf::Arena` na obiekty per-tick, `Clear()` zamiast rekonstrukcji (zachowuje pojemność buforów), `SerializeToArray()` do preallokowanego bufora
- TS: `protobuf-es` (bufbuild) — natywny TypeScript, tree-shaking, brak runtime reflection

---

### D3 — Tick: symulacja 10 Hz, snapshot 5 Hz

**Historia korekt (istotna):**

| Iteracja | Założenie | Rekomendacja |
|---|---|---|
| 1 | „MOBA/RTS" | sim 20 Hz / send 10 Hz |
| 2 | brak predykcji klienta | sim 20 Hz / send **20 Hz** — nic nie maskuje latencji |
| 3 | **gra kafelkowa, nie jednostkowa** | sim 10 Hz / send **5 Hz** |

**Dlaczego finalnie 5 Hz:**

- Nie ma reakcji, uników ani celowania. Nie istnieje gameplay zależny od czasu reakcji.
- **Interpolacja klienta nie istnieje z definicji** — własność kafelka jest wartością dyskretną, nie da się jej interpolować. To usuwa cały bufor interpolacji (~100–150 ms), który przy grach jednostkowych jest głównym źródłem odczuwalnej latencji.
- Płynność wizualna realizowana jest animacją: przejęty kafelek robi fade-in przez ~150 ms po stronie klienta. Wygląda gładko niezależnie od tego, że dane przyszły skokowo.

**Budżet latencji:** klik → do 200 ms do następnego snapshotu → RTT/2 (~20 ms) → widoczne. **~220 ms.** Przy ataku angażującym 50% populacji na kilkanaście sekund jest to niezauważalne.

**Rozdzielenie sim/send:** sim 10 Hz daje granularność ekonomii (płynny przyrost populacji), send 5 Hz połowi ruch. Zmiany akumulują się przez 2 ticki w **zbiorze** brudnych kafelków — kafelek, który zmienił właściciela dwa razy w oknie, wysyłany jest raz, z finalnym właścicielem. Naturalna deduplikacja.

---

### D4 — Keyframe tylko przy wejściu i reconnekcie

**Rozważane:** cykliczny pełny stan co 3–5 s jako zabezpieczenie.

**Decyzja odrzucona po przeliczeniu.**

RLE pełnej własności dla mapy 2 mln kafelków w połowie meczu:

> 1000 wierszy × ~25 przecięć granic terytorialnych na wiersz × ~3 B ≈ **60–80 KB**

Rozsyłanie tego co 3 s do 64 graczy = ~5 MB/s samych keyframe'ów — **więcej niż cały pozostały ruch razem wzięty**.

Cykliczne keyframe'y były pasem bezpieczeństwa na wypadek gubienia ramek. Ale WebSocket działa po TCP, więc **dostarczenie i kolejność są gwarantowane**. Łańcuch delt nie może się rozjechać. Keyframe potrzebny jest wyłącznie tam, gdzie klient nie ma stanu bazowego: przy wejściu do meczu i przy reconnekcie.

**Jedyny wyjątek:** jeśli kiedykolwiek pominiesz ramkę dla wolnego klienta (backpressure), łańcuch delt się łamie i musisz mu wysłać keyframe. Prostsze rozwiązanie: przy ~1 KB/snapshot rosnący bufor wyjściowy oznacza, że klient i tak jest martwy — **rozłącz go po przekroczeniu progu (np. 256 KB)** zamiast kombinować z odbudową.

---

### D5 — Kodowanie delt: grupowanie po właścicielu + delty posortowanych indeksów

**To jest najważniejsza optymalizacja w całym protokole.**

**Problem:** indeks kafelka w zakresie 2 mln to **21 bitów** → surowy varint kosztuje **3 bajty**. Naiwne pary `(tileIndex, ownerId)` = 4 B/kafelek.

**Rozwiązanie:**
1. Grupuj zmienione kafelki po **nowym właścicielu** — `ownerId` płacisz raz na grupę, nie raz na kafelek
2. Sortuj indeksy rosnąco w obrębie grupy
3. Koduj **różnice między kolejnymi indeksami**, nie same indeksy

Ekspansja jest z natury **przestrzennie ciągła** (postępuje wzdłuż granicy), więc różnice są małe i varint mieści się w 1 bajcie.

```proto
message TileDeltaGroup {
  uint32 owner_id = 1;
  repeated uint32 index_deltas = 2 [packed = true];
}
```

**Efekt: ~4 B/kafelek → ~1,2 B/kafelek.** Ponad trzykrotna redukcja przy zerowym wzroście złożoności po stronie klienta (prosta pętla akumulująca).

**Rozważana alternatywa:** RLE wzdłuż skanu row-major — `(startDelta, runLength, ownerId)`. Lepsza dla keyframe'ów (i tam jest używana), gorsza dla delt, bo pojedyncze przejęcia na granicy rzadko tworzą długie ciągi w kolejności row-major.

---

### D6 — Brak predykcji klienta

**Decyzja:** klient nie symuluje niczego. Wyłącznie natychmiastowa **odpowiedź wizualna** na wejście.

**Konsekwencja upraszczająca:** znika `last_processed_seq` w snapshocie. Bez predykcji nie ma reconciliation, więc nie ma czego potwierdzać. To istotne, bo **utrzymuje snapshot w 100% wspólny** — ani jednego pola per gracz w głównym strumieniu.

**Co zostaje potrzebne:** wąski kanał zwrotny na odrzucenia — `CommandRejected { seq, reason }` (nieprawidłowy cel, brak złota, brak wspólnej granicy). Zdarza się rzadko, więc per-gracz bez problemu.

**Maskowanie latencji (wzorzec Blizzarda):** w momencie kliknięcia odpalają się **lokalnie i natychmiast**: podświetlenie celu, marker, dźwięk potwierdzenia, animacja suwaka. Gracz odbiera to jako zerową latencję, mimo że stan świata zmieni się 220 ms później.

---

### D7 — Jeden proces = jeden mecz

**Uzasadnienie:**
- Zerowa izolacja do zbudowania — crash zabija jeden mecz, nie serwer
- Brak współdzielonego stanu mutowalnego → **brak mutexów w całym kodzie symulacji**
- Wycieki pamięci przestają mieć znaczenie przy 10–25-minutowym cyklu życia procesu

**Bardzo przyjemna właściwość, której nie wolno zepsuć:** mecze mają **ograniczony czas życia (≤25 min)**. Deploy nowej wersji to „przestań alokować do starych procesów i poczekaj 25 minut". Zero migracji stanu na żywo, zero wersjonowania protokołu między współistniejącymi instancjami. Gry z sesjami bez limitu czasu muszą to rozwiązywać boleśnie.

> **Uwaga projektowa:** nie wprowadzać meczów bez limitu czasu bez świadomej rewizji tej decyzji.

**Koszt:** wymaga orkiestratora (patrz §9) i rozwiązuje problem routingu TLS (D9).

---

### D8 — Boost.Beast, nie samo Asio, nie uWebSockets

**Rozważane:** Boost.Asio / Boost.Beast / uWebSockets / libwebsockets.

**Samo Asio nie wystarczy.** Asio daje gniazda i model asynchroniczny, ale **zero implementacji RFC 6455** — handshake, framing, maskowanie, fragmentacja i close handshake trzeba by napisać samodzielnie. Beast siedzi na Asio i to wszystko ma.

**uWebSockets jest szybszy**, ale przy 64 połączeniach × 5 Hz × ~1 KB różnica wydajnościowa jest kompletnie bez znaczenia. Beast wygrywa czytelnością i integracją z resztą Boost.

**Model:** jeden `io_context`, **jeden wątek**, `steady_timer` napędzający tick. Zero mutexów. Przy procesie na mecz współbieżność jest niepotrzebna — 2 mln kafelków z indeksem granic (D-struktury, §6) to rząd setek mikrosekund CPU na tick.

---

### D9 — Terminacja TLS na proxy, ingress na 443

**Problem, który ujawnia się dopiero na produkcji:** proces-na-mecz oznacza, że każda instancja słucha na osobnym porcie. Ale:

- Certyfikat TLS musiałby pokrywać `wss://gs-3.example.com:7031` — zarządzanie certyfikatami per-port to koszmar
- **Firewalle korporacyjne i część sieci mobilnych blokują porty inne niż 443** — a klient jest w przeglądarce, więc środowisko sieciowe użytkownika jest poza naszą kontrolą

**Decyzja:** jedno wejście na **443**, routing po ścieżce.

```
wss://gs.example.com/match/{matchId}
        ↓ Envoy / nginx
    127.0.0.1:{port procesu}
```

**Efekty uboczne, wszystkie pozytywne:**
- TLS terminowany raz, w jednym miejscu — rotacja certyfikatów w jednym miejscu
- Beast mówi gołym `ws://` po sieci wewnętrznej → **odpada `boost::asio::ssl` i cała obsługa certyfikatów w C++**
- Dodatkowy hop kosztuje ułamek milisekundy

**Konfiguracja proxy — obowiązkowo:**
- `proxy_buffering off` — inaczej nginx buforuje snapshoty
- długi `proxy_read_timeout` — inaczej zrywa bezczynne połączenia w spokojnych fazach meczu

---

### D10 — Symulacja deterministyczna

**Decyzja:** pełny determinizm, stały przecinek / arytmetyka całkowitoliczbowa, bez `float`.

**Co to daje:**

**① Replaye za darmo.** Nie nagrywasz snapshotów, tylko **seed + log komend** `(tick, playerId, order)`.

| Metoda | Rozmiar (mecz 15 min) |
|---|---|
| Log komend | **~600 KB** |
| Nagrane snapshoty | ~9 MB |

Rząd wielkości różnicy, a plik nadaje się do analizy balansu, heatmap i wykrywania botów.
*Minus:* przewijanie wymaga re-symulacji → wrzucaj pełny stan co ~30 s ticków jako punkty zaczepienia.

**② Odzyskiwanie po crashu.** To większa korzyść niż replaye. Bez tego jeden segfault w 18. minucie niszczy mecz 100+ aktorom. Z logiem komend: nowy proces, re-symulacja, keyframe do wszystkich. 15 min × 10 Hz = 9000 ticków — realnie **1–3 sekundy re-simu**. Gracze widzą zacinkę, nie stratę meczu.

**③ Weryfikowalność.** Hash stanu świata pozwala odtworzyć każdy zgłoszony bug deterministycznie.

**Determinizm cicho gnije — zabezpiecz go od pierwszego dnia.** Patrz §8.

---

### D11 — Dwa kanały realtime, świadomie

| Kanał | Technologia | Format | Częstotliwość | Zastosowanie |
|---|---|---|---|---|
| Lobby | **SignalR** | JSON | kilka wiadomości/min | dołączenia, ready, ustawienia, start |
| Mecz | **goły WebSocket** | protobuf | 5 Hz | snapshoty, komendy |

**Uzasadnienie:** to nie jest duplikacja, tylko dwa różne problemy. Lobby to niskoczęstotliwościowe zdarzenia, gdzie SignalR daje darmową integrację z ASP.NET, automatyczny reconnect, fallbacki i wygodny klient `@microsoft/signalr` w Angularze. Protobuf w lobby byłby narzutem bez korzyści. Mecz to strumień binarny do C++, gdzie SignalR nie ma czego szukać.

---

### D12 — Slot index u8 + tablica aktorów

**Problem:** 100–254 aktorów to **dokładnie na granicy** pojemności bajtu.

**Rozważane:**
- `uint8` bezpośrednio jako ID aktora → twardy sufit 253 slotów, brak marginesu
- `uint16` w tablicy kafelków → +2 MB na proces; przy 250 procesach **+500 MB RAM**

**Decyzja: indirekcja.** Tablica kafelków przechowuje **kompaktowy slot index (u8)**, a tożsamość aktora żyje w osobnej tablicy slotów.

```
owner[tileIndex] → u8 slot          // 0 = pustkowie, 255 = woda, 1..254 = slot
slots[slot] → { actorId, kind, playerId?, botProfile?, color, ... }
```

**Korzyści:**
- Tablica kafelków zostaje 2 MB zamiast 4 MB
- Sloty są **per-mecz i ograniczone z definicji** — sufit 254 jest wtedy właściwością meczu, nie systemu
- Zmiana skali w przyszłości dotyka jednej tablicy, nie 2-milionowego bufora
- W protokole `owner_id` jest amortyzowane per grupę (D5), więc jego szerokość nie wpływa na ruch sieciowy

**Rezerwacje:** `0` = pustkowie/neutralne, `255` = woda/nieprzejezdne, `1..254` = aktorzy.

---

### D13 — Mapa jako jeden binarny plik dla obu stron

Mapy są stałe (nie generowane), więc terrain to **statyczny, niezmienny asset**.

```
[magic 4B]["TMAP"]
[version u16]
[width u16][height u16]
[terrainTypeCount u8]
[reserved 5B]
[raw: width * height bajtów, 1 bajt = typ terenu]
```

**Przeglądarka:**
```ts
const buf = await (await fetch(url)).arrayBuffer();
const terrain = new Uint8Array(buf, HEADER_SIZE);
```
Zero konwersji, zero canvas round-tripu.

> **Rozważane i odrzucone:** PNG z paletą indeksowaną. Przeglądarka dekoduje natywnie, ale rozwija do RGBA (8 MB) i wymaga round-tripu przez canvas + `getImageData` żeby odczytać wartości. Surowy binarny plik jest prostszy i szybszy.

Serwuj z `Content-Encoding: gzip` — mapy ląd/woda kompresują się rewelacyjnie, **2 MB → ok. 80–200 KB**.

**C++:** `mmap` read-only. Ładna właściwość: **wszystkie procesy meczowe na tej samej maszynie współdzielą jedno mapowanie** przez page cache. 250 procesów, 2 MB fizycznie.

**Adresowanie hashem:** `/maps/{id}/{sha256}/terrain.bin` z `Cache-Control: max-age=31536000, immutable`. Hash leci w `MatchInit`; klient porównuje z cache'em i dociąga przy niezgodności. **Eliminuje całą klasę desyncu ze starego cache'a.**

---

### D14 — Reconnect: OTWARTE (rekomendacja: zaimplementować)

**Stan obecny:** nie planowany.

**Rekomendacja: zaimplementować przed pierwszym publicznym testem.**

**Uzasadnienie:** mecz trwa 10–25 minut, klient siedzi w przeglądarce, gdzie rozłączenia są **normą, nie wyjątkiem**: uśpienie laptopa, przejście Wi-Fi → LTE, przypadkowy F5, throttling zakładki w tle. Bez reconnectu jedno mignięcie sieci wyklucza gracza z 20-minutowej partii.

**Kluczowa obserwacja: 90% reconnectu już istnieje.** Są keyframe'y (D4), nie ma predykcji (D6), nie ma stanu per klient (D5). Reconnect na poziomie netcode'u to: nowy WS → bilet → keyframe → grasz dalej.

Do dorobienia zostają trzy rzeczy, wszystkie **poza protokołem**:
1. Bilet musi dać się **wydać ponownie** dla trwającego meczu (TTL 60 s zostaje, C# wystawia nowy na żądanie)
2. Proces trzyma slot gracza żywy przez 60–120 s zamiast go zwalniać
3. Decyzja projektowa: co robi terytorium rozłączonego gracza — zamarza czy przejmuje je bot

Szacowany koszt: 1–2 dni. Punkt 3 to decyzja gameplayowa, nie techniczna.

---

## 4. Podział warstw

### 4.1 Frontend — Angular 22

**Kluczowa decyzja architektoniczna: mapa nigdy nie wchodzi do Angulara.**

Nie da się trzymać 2 mln kafelków w sygnałach ani renderować przez DOM. Podział jest ostry i nienegocjowalny.

```
src/app/
├── core/
│   ├── auth/              AuthService, jwt.interceptor, refresh flow
│   ├── api/               klienci HTTP generowani z OpenAPI (NSwag)
│   └── config/            endpointy, CDN base URL
├── lobby/
│   ├── lobby.store.ts         sygnały: gracze, ready, ustawienia, wybór mapy
│   ├── lobby-hub.service.ts   SignalR — zdarzenia lobby
│   └── ui/                    DaisyUI: lista graczy, przyciski, chat
├── game/
│   ├── net/
│   │   └── game-socket.worker.ts   WS + protobuf + bufor kafelków    ⟵ WORKER
│   ├── render/
│   │   ├── map-renderer.ts         OffscreenCanvas, chunki, ImageData ⟵ WORKER
│   │   ├── camera.service.ts       pan/zoom, transform ekran↔kafelek
│   │   └── input.service.ts        klik → tileIndex → właściciel
│   ├── state/
│   │   └── game.store.ts           sygnały TYLKO dla UI
│   ├── proto/                      protobuf-es (codegen z .proto)
│   └── ui/                         DaisyUI: pasek zasobów, suwak %, menu budowy
└── profile/                        statystyki, historia meczów, replaye
```

#### Model wątkowy

Cały WebSocket, dekodowanie protobufa, tablica własności i renderowanie mapy żyją w **Web Workerze**. Główny wątek dostaje `transferControlToOffscreen()` na canvasie i obsługuje wyłącznie UI.

Worker → główny wątek: tylko lekki stan UI (populacja, złoto, ranking) — kilkaset bajtów, ~1–2 razy na sekundę.

> **Dlaczego to jest niezbywalne:** gdyby renderowanie było w głównym wątku, change detection Angulara odpalałoby się przy każdej klatce. W Angularze 22 użyj trybu zoneless, ale i tak trzymaj pętlę renderowania całkowicie poza cyklem Angulara.

#### Struktury danych w workerze

| Struktura | Typ | Rozmiar |
|---|---|---|
| `terrain` | `Uint8Array` | 2 MB (statyczna, z CDN) |
| `owner` | `Uint8Array` | 2 MB (dynamiczna, slot index) |
| `pixels` | `Uint32Array` | **8 MB** (bufor ImageData) |
| `slots` | `Array<SlotInfo>` | pomijalny |

Razem ~12 MB typed arrays. Akceptowalne w przeglądarce.

#### Rendering — chunki, nie bounding box

`putImageData` całego bufora to 8 MB memcpy. Przy 5 Hz to 40 MB/s przepalone bez potrzeby.

**Bounding box nie zadziała** — ekspansja dzieje się jednocześnie u 100+ aktorów w różnych rogach mapy, więc jeden prostokąt obejmujący wszystkie zmiany to zwykle cała mapa.

**Rozwiązanie: podział mapy na chunki 128×128** (≈ 16 × 8 = 128 chunków). Śledź brudne chunki, `putImageData` tylko na nich.

**Rozdziel dwie pętle:**

| Pętla | Częstotliwość | Zadanie |
|---|---|---|
| Sieciowa | 5 Hz (przyjście snapshotu) | patchuj `owner`, patchuj `pixels`, `putImageData` na brudnych chunkach → trwały `OffscreenCanvas` 2000×1000 |
| Renderu | 60 Hz (rAF) | jeden `drawImage(offscreen, sx, sy, sw, sh, 0, 0, dw, dh)` z transformacją kamery |

Ustaw `imageSmoothingEnabled = false` — inaczej przy zoomie dostaniesz rozmytą papkę zamiast pikselowej mapy.

---

### 4.2 Meta-serwer — ASP.NET Core + EF Core + PostgreSQL

**Zasada: meta nie wie nic o symulacji.** Nie zna kafelków, nie zna populacji w trakcie meczu. Dostaje wynik na końcu.

```
Meta/
├── Api/
│   ├── AuthController          rejestracja, login, refresh
│   ├── LobbyController         CRUD lobby, dołączanie
│   ├── MapController           katalog map, URL-e CDN
│   ├── MatchController         historia, replaye (odczyt)
│   ├── ProfileController       statystyki, ranking
│   └── Internal/
│       └── GameServerController    ⟵ ODDZIELNY port/sieć, mTLS
├── Hubs/
│   └── LobbyHub                SignalR: dołączenia, ready, start
├── Domain/
│   ├── User, RefreshToken
│   ├── Lobby, LobbySlot
│   ├── Match, MatchParticipant
│   └── MapDefinition, Replay
├── Services/
│   ├── TicketService           podpisywanie biletów meczowych (ECDSA P-256, §5③)
│   ├── AllocationService       rozmowa z agentem/orkiestratorem
│   └── MatchResultService      idempotentny zapis wyników
└── Infrastructure/
    ├── AppDbContext (EF Core)
    └── ReplayBlobStore (S3 / Azure Blob)
```

#### Bezpieczeństwo — `Internal/` musi być odseparowany

Game-serwery uwierzytelniają się **mTLS-em albo współdzielonym sekretem, na osobnym porcie, najlepiej w sieci prywatnej**.

> Wystawienie endpointu „zapisz wynik meczu" na publiczne API to prosta droga do fabrykowania rankingu.

#### Model `MapDefinition`

```
Id, Name, Width, Height, Sha256, CdnPath, MaxActors, SpawnPoints[]
```

Punkty startowe zdefiniowane per mapa — daje kontrolę nad balansem, czego generacja proceduralna by nie dała.

---

### 4.3 Game-serwer — C++23 + Boost.Beast

```
gameserver/
├── net/
│   ├── ws_server.cpp        Beast acceptor, upgrade HTTP→WS
│   ├── session.cpp          jedno połączenie, kolejka wyjściowa, backpressure
│   └── codec.cpp            protobuf encode/decode, arena
├── sim/
│   ├── world.cpp            terrain (mmap) + owner + indeks granic
│   ├── territory.cpp        ekspansja: wydawanie siły ataku na kafelki granicy
│   ├── economy.cpp          przyrost populacji, generacja złota
│   ├── buildings.cpp        miasta: koszt, wpływ na przyrost
│   ├── bots.cpp             AI botów — MUSI być deterministyczne
│   └── rng.cpp              własny PCG, seed z meczu
├── tick/
│   └── loop.cpp             steady_timer, stały krok, akumulator
├── cmd/
│   └── queue.cpp            bufor komend, sortowanie (playerId, seq)
├── state/
│   ├── delta_builder.cpp    zbiór brudnych kafelków → grupy + delty varint
│   └── keyframe.cpp         pełny stan RLE row-major
├── replay/
│   ├── command_log.cpp      append-only, fsync co 1 s
│   └── hasher.cpp           xxHash stanu co N ticków
└── meta/
    ├── ticket_verifier.cpp  offline weryfikacja podpisu ECDSA P-256
    └── meta_client.cpp      HTTP do C# na końcu meczu
```

#### Kluczowa struktura wydajnościowa: indeks kafelków granicznych

**Nie skanuj 2 mln kafelków co tick.** Każdy aktor trzyma zbiór swoich kafelków granicznych (mających sąsiada o innym właścicielu). Ekspansja przegląda wyłącznie ten zbiór.

Aktor z dużym terytorium ma może kilka tysięcy kafelków granicy — **trzy rzędy wielkości mniej pracy** niż skan całej mapy.

**Aktualizacja:** gdy kafelek zmienia właściciela, sprawdzasz jego 4 sąsiadów i dodajesz/usuwasz ich z odpowiednich zbiorów granicznych. Amortyzowane O(1) na zmianę.

> **Uwaga na determinizm:** zbiór granicy musi być iterowany w **posortowanej kolejności indeksów kafelków**, nie w kolejności `unordered_set`. Patrz §8.

#### Weryfikacja biletu — offline

`ticket_verifier` weryfikuje podpis ECDSA P-256 **kluczem publicznym wbudowanym w obraz / pobranym przy starcie**. Żadnego callbacku HTTP do C# przy każdym połączeniu.

> Game-serwer nie może zależeć od dostępności meta-serwisu w ścieżce krytycznej. Restart ASP.NET nie ma prawa zerwać trwających meczów.

---

## 5. Protokół sieciowy — pełny przepływ

### ① Logowanie
**Angular → Meta** · HTTPS/JSON · raz na sesję

```
POST /api/auth/login
  → { accessToken (15 min), refreshToken (30 dni) }
```

### ② Lobby
**Angular ↔ Meta** · SignalR/JSON · kilka wiadomości/min

Hub `/hubs/lobby`: `PlayerJoined`, `PlayerLeft`, `PlayerReady`, `SettingsChanged`, `MatchStarting`

### ③ Alokacja
**Meta → Orkiestrator** · HTTP wewnętrzny · raz na mecz

Meta otrzymuje `host:port`, generuje **bilet per gracz**:

```
JWT ECDSA P-256 (ES256), TTL 60 s
claims: { playerId, matchId, slot, nonce }
```

> **Korekta (30.07.2026).** Wcześniejsza wersja mówiła „Ed25519" — a .NET 10 nie ma Ed25519
> w BCL: `System.Security.Cryptography` daje `ECDsa` oraz postkwantowe `MLDsa`/`SlhDsa`.
> Podpis Ed25519 wymagałby natywnej zależności (NSec na libsodium albo BouncyCastle), więc
> wybrana została **ECDSA P-256**: zero nowych zależności po stronie meta, a po stronie
> game-serwera OpenSSL, który i tak jest w obrazie. Różnica w rozmiarze podpisu (~70 B DER
> zamiast 64 B) występuje raz na mecz. Uzasadnienie w
> [plan-alokacji-meczu.md](plan-alokacji-meczu.md) §6.1.
>
> **Dopisek (30.07.2026).** Zdanie o rozmiarze było zbędnie ostrożne: w JWS podpis ES256 to
> **surowe `R‖S`, 64 bajty** (RFC 7518), a nie DER — dokładnie tyle, co Ed25519. Kodowanie DER
> pojawiłoby się wyłącznie przy własnym formacie biletu, a bilet jest JWT. Po stronie .NET
> wychodzi to samo z siebie (`ECDsa.SignData` domyślnie zwraca IEEE P-1363), ale **OpenSSL
> oczekuje DER**, więc `ticket_verifier` musi złożyć `ECDSA_SIG` z dwóch połówek, zanim
> zweryfikuje. Prawdziwy koszt tej krzywej to więc dwadzieścia linii w C++, a nie bajty w ruchu —
> i jest to koszt jednorazowy. Szczegóły w [plan-serwera-gry.md](plan-serwera-gry.md) §3.4.

Rozsyła przez SignalR: `MatchReady { wsUrl, ticket }`

### ④ Terrain
**Angular → CDN** · HTTPS · raz na życie cache'a

```
GET /maps/{id}/{sha256}/terrain.bin
Cache-Control: max-age=31536000, immutable
Content-Encoding: gzip          (2 MB → ~80–200 KB)
```

### ⑤ Mecz
**Angular ↔ GameServer** · WSS/protobuf

**Handshake:**
```
C→S  ClientHello { ticket }
S→C  MatchInit   { mapId, mapSha256, tickRate, yourSlot, slots[], seed }
S→C  Snapshot    { is_keyframe = true, runs[...] }        ~60–80 KB
```

**Pętla (5 Hz):**
```
S→C  Snapshot { tick, deltas[...], me, others[...] }      ~0,7–2,4 KB
```

**Sporadycznie:**
```
C→S  Command          { seq, AttackOrder | BuildCityOrder }   ~0,1/s na gracza
S→C  CommandRejected  { seq, reason }                         rzadko
S→C  MatchEnd         { standings[] }                         raz
C↔S  Ping / Pong                                              co 5 s
```

> **Uwaga:** przeglądarka **nie daje JavaScriptowi dostępu do natywnych ramek ping/pong WebSocketa**. RTT po stronie klienta wymaga własnych wiadomości aplikacyjnych. W drugą stronę działa: serwer wysyła ramkę ping, przeglądarka odpowiada automatycznie, więc detekcja martwych połączeń po stronie C++ jest darmowa.

### ⑥ Wynik
**GameServer → Meta** · HTTPS wewnętrzny (mTLS) · raz na mecz

```
POST /internal/matches/{matchId}/result    idempotentnie po matchId
POST /internal/matches/{matchId}/replay    log komend, ~600 KB
```

Obowiązkowo: **retry z backoffem + lokalny WAL**. Jeśli meta akurat się restartuje, nie wolno zgubić wyników rankingowych.

---

## 6. Schema protobuf (draft)

```proto
syntax = "proto3";
package game;

// ─────────────────────────── Klient → Serwer ───────────────────────────

message ClientMsg {
  oneof msg {
    ClientHello hello   = 1;   // pierwsza ramka po połączeniu
    Command     command = 2;
    Ping        ping    = 3;
  }
}

message ClientHello {
  string ticket = 1;           // JWT ECDSA P-256 z meta-serwera
}

message Command {
  uint32 seq = 1;
  oneof order {
    AttackOrder    attack = 10;
    BuildCityOrder build  = 11;
  }
}

message AttackOrder {
  uint32 target_slot    = 1;   // 0 = pustkowie
  uint32 population_pct = 2;   // 1..100
}

message BuildCityOrder {
  uint32 tile_index = 1;
}

// ─────────────────────────── Serwer → Klient ───────────────────────────

message ServerMsg {
  oneof msg {
    MatchInit       init     = 1;
    Snapshot        snapshot = 2;
    CommandRejected rejected = 3;
    MatchEnd        end      = 4;
    Pong            pong     = 5;
  }
}

message MatchInit {
  string          map_id      = 1;
  bytes           map_sha256  = 2;
  uint32          tick_rate   = 3;
  uint32          your_slot   = 4;
  uint64          seed        = 5;
  repeated SlotInfo slots     = 6;
}

message SlotInfo {
  uint32 slot      = 1;
  string name      = 2;
  uint32 color_rgb = 3;
  bool   is_bot    = 4;
}

message Snapshot {
  uint32 tick        = 1;
  bool   is_keyframe = 2;

  repeated OwnershipRun   runs   = 3;  // tylko keyframe
  repeated TileDeltaGroup deltas = 4;  // zwykły tick

  repeated PublicState others = 5;     // co 5. snapshot (1 Hz)
}

message OwnershipRun {
  uint32 start_delta = 1;   // różnica od końca poprzedniego runu
  uint32 length      = 2;
  uint32 slot        = 3;
}

// ⟵ SERCE OPTYMALIZACJI, patrz D5
message TileDeltaGroup {
  uint32 slot = 1;
  repeated uint32 index_deltas = 2 [packed = true];
}

message PublicState {
  uint32 slot            = 1;
  uint32 territory_tiles = 2;
}

// wysyłany OSOBNĄ wiadomością, żeby nie łamać wspólnego bufora
message MyState {
  uint32 population   = 1;
  uint32 gold         = 2;
  uint32 pop_income   = 3;
  uint32 gold_income  = 4;
  uint32 attack_force = 5;
  uint32 cities       = 6;
}
```

### Rozdzielenie stanu gracza

Przy 254 aktorach pełny stan dla każdego to ~3 KB na aktualizację. Podział:

| Wiadomość | Zawartość | Odbiorca | Częstotliwość |
|---|---|---|---|
| `MyState` | populacja, złoto, przychody, siła ataku | tylko właściciel | 5 Hz |
| `PublicState[]` | `{ slot, territory_tiles }` | wszyscy | 1 Hz |

`others` służy wyłącznie do rankingu → ~4 B × 254 ≈ **1 KB**, wysyłane co 5. snapshot.

Cudze złoto i populacja to informacja, której gracz nie powinien mieć — to naturalny element ekonomii informacji w tym gatunku, nie tylko optymalizacja.

> **Ważne:** `MyState` jest per-gracz, więc **łamie wspólny bufor**. Serializuj `Snapshot` bez niego raz (wspólne bajty do broadcastu), a `MyState` wyślij jako osobną, ~15-bajtową wiadomość obok. Broadcast zostaje jednym `memcpy`.

### Trik: prekodowanie wspólnego bufora

Brak fog of war oznacza, że **wszyscy dostają identyczny `Snapshot`**. Serializuj **całe `ServerMsg`** raz na tick i rozsyłaj te same bajty — tag `oneof` jest już w buforze, nic nie doklejasz per gracz.

Złożoność serializacji: **O(zmienione kafelki)**, nie O(aktorzy × kafelki).

---

## 7. Budżet wydajnościowy

### Ruch sieciowy

| Scenariusz | kafelki/s | snapshot @ 5 Hz | na gracza | na mecz (64 ludzi) |
|---|---|---|---|---|
| Spokój | ~1 000 | 200 kafelków ≈ 240 B | ~2 KB/s | ~1 Mbit/s |
| Typowo | ~3 000 | 600 kafelków ≈ 720 B | ~5 KB/s | ~2,5 Mbit/s |
| Szczyt | ~10 000 | 2 000 kafelków ≈ 2,4 KB | ~13 KB/s | ~6,5 Mbit/s |

Keyframe przy wejściu: **60–80 KB jednorazowo** na gracza.

> **Dźwignia, gdy szczyt okaże się wyższy:** globalny limit kafelków przejmowanych na tick. To jednocześnie **narzędzie balansu gameplayowego**, nie tylko optymalizacja sieciowa — warto je mieć niezależnie.

### Pamięć

| | Rozmiar |
|---|---|
| Proces meczu (terrain mmap współdzielony + owner + granice + protobuf arena) | ~10–20 MB |
| Klient (typed arrays) | ~12 MB |

### Skala serwerowa

Przy 20 ludziach na mecz i meczach 15-minutowych:

| CCU | Równoległych meczów | RAM | CPU |
|---|---|---|---|
| 1 000 | 50 | ~1 GB | < 1 rdzeń |
| 5 000 | 250 | ~5 GB | ~2 rdzenie |
| 20 000 | 1 000 | ~20 GB | ~8 rdzeni |

2 mln kafelków z indeksem granic to rząd **setek mikrosekund CPU na tick**. Do 5 000 CCU mieścisz się na dwóch–trzech maszynach.

---

## 8. Determinizm — checklist

> **Determinizm nie jest właściwością, którą się „ma". Jest właściwością, która działa do momentu, aż ktoś doda jedną linijkę.**
>
> Bez zabezpieczenia odkryjesz to w 8. miesiącu, gdy replaye przestaną się zgadzać, i nie będziesz wiedział, który commit to zepsuł.

### Zabezpieczenie (zrób to od pierwszego dnia)

**Hash stanu świata co N ticków** (xxHash po tablicy `owner` + stanie RNG + stanie ekonomii aktorów) zapisywany w replayu i weryfikowany przy re-symulacji **w CI**. Jeden test integracyjny przepuszczający nagrany mecz i porównujący hashe.

### Pułapki, których stały przecinek NIE eliminuje

| Pułapka | Objaw | Rozwiązanie |
|---|---|---|
| `std::unordered_map` / `unordered_set` | kolejność iteracji zależy od historii wstawień i implementacji | gęste tablice indeksowane ID; iteracja po posortowanych indeksach |
| Sortowanie / klucze po wskaźnikach | ASLR robi ruletkę między uruchomieniami | nigdy adres jako klucz ani kryterium sortowania |
| `std::sort` jest niestabilny | elementy równe zamieniają się miejscami | `stable_sort` albo entity ID jako ostateczny tiebreaker |
| RNG spoza symulacji | `rand()`, `random_device`, cokolwiek od zegara | własny PCG seedowany z seeda meczu; **stan RNG w hashu i keyframie** |
| Kolejność komend z tego samego ticka | zależy od kolejności przybycia z sieci | sortuj po `(playerId, seq)` przed aplikowaniem |
| Niezainicjalizowana pamięć / padding | losowe bajty w hashu | `memset` struktur wchodzących do hasha |

### Boty — trzy rzeczy nieoczywiste

**① AI botów musi być deterministyczne, inaczej replay jest martwy.**
Boty nie wysyłają komend przez sieć, więc nie ma ich w logu komend — są **odtwarzane przez re-symulację**. Decyzja bota nie może zależeć od zegara ściennego ani od kolejności iteracji kontenerów. Wyłącznie: stan świata + `rng.next()`.

**② Wersjonuj symulację w nagłówku replaya.**
Skoro boty są regenerowane, **zmiana ich AI unieważnia wszystkie stare replaye**. Wstaw build hash sim-a do pliku i odmawiaj odtwarzania przy niezgodności — inaczej dostaniesz cicho rozjeżdżające się replaye zamiast czystego błędu.

**③ Boty generują delty kafelków dokładnie jak gracze.**
Przy 100–254 aktorach churn kafelków rośnie proporcjonalnie. To główny czynnik ryzyka dla budżetu z §7.

---

## 9. Deployment i orkiestracja

### Reality check przed sięgnięciem po Kubernetes

Przy 250 równoległych meczach mówimy o **~5 GB RAM i ~2 rdzeniach**. Przy tej skali **Agones i Kubernetes to nadmiarowa złożoność operacyjna**.

### Wariant A — własny agent (rekomendowany na start)

Mały agent na każdej VM-ce:
- Meta woła „daj slot" → agent forkuje proces, zwraca port
- Health check przez unix socket albo heartbeat do meta
- Reaping po `MatchEnd` albo po timeoucie

Dzień roboty, zero klastra do utrzymywania.

### Wariant B — Agones / Kubernetes

Bierz, gdy potrzebujesz **multi-region z autoskalowaniem** albo celujesz w dziesiątki tysięcy CCU. Wtedy dostajesz alokację, pulę Ready i drenowanie z pudełka.

### Pula ciepłych procesów

```
rozmiar puli = szczytowe tempo startu meczów × czas spawnu × 2
```

Przy spawnie ~200 ms i 5 meczach/s wystarczy pula rzędu 2–5 procesów. Spawn + warmup w momencie, gdy gracze już czekają w lobby, to kiepskie pierwsze wrażenie.

### Deploy

Dzięki D7 (proces = mecz, ≤25 min): **przestań alokować do starych procesów i poczekaj**. Żadnej migracji stanu, żadnego wersjonowania protokołu między instancjami.

---

## 10. Otwarte kwestie

| # | Kwestia | Status |
|---|---|---|
| 1 | **Reconnect** — patrz D14 | rekomendacja: zaimplementować przed publicznym testem |
| 2 | Zachowanie terytorium rozłączonego gracza (zamarza / przejmuje bot) | decyzja gameplayowa |
| 3 | Dokładna funkcja przyrostu populacji (całkowitoliczbowa!) | do zaprojektowania |
| 4 | Koszt przejęcia kafelka — zależność od terenu i gęstości obrońcy | do zaprojektowania |
| 5 | Globalny limit kafelków/tick jako dźwignia balansu | do ustalenia empirycznie |
| 6 | Spectatorzy | poza zakresem v1 |
| 7 | Odtwarzacz replayów (klient re-symulujący w WASM?) | poza zakresem v1 |

---

## Załącznik — indeks decyzji

| ID | Decyzja | Sekcja |
|---|---|---|
| D1 | Komendy przez WebSocket, nie REST | §3 |
| D2 | Protocol Buffers z kwantyzacją | §3 |
| D3 | Sim 10 Hz / snapshot 5 Hz | §3 |
| D4 | Keyframe tylko przy wejściu | §3 |
| D5 | Grupowanie po właścicielu + delty indeksów | §3 |
| D6 | Brak predykcji klienta | §3 |
| D7 | Jeden proces = jeden mecz | §3 |
| D8 | Boost.Beast | §3 |
| D9 | TLS na proxy, ingress 443 | §3 |
| D10 | Symulacja deterministyczna | §3 |
| D11 | SignalR (lobby) + goły WS (mecz) | §3 |
| D12 | Slot index u8 + tablica aktorów | §3 |
| D13 | Mapa jako wspólny plik binarny | §3 |
| D14 | Reconnect — otwarte | §3 |
