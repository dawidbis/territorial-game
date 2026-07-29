# Plan: alokacja instancji serwera gry i wpuszczanie graczy

Jak zamienić zamrożony roster w żywy mecz i doprowadzić do niego każdego gracza.

**Status:** propozycja do zatwierdzenia
**Zakres:** domknięcie luki nr 1–2 z [dokumentacja-aplikacji.md](dokumentacja-aplikacji.md) §9
**Podstawa:** [architektura-gry-terytorialnej.md](architektura-gry-terytorialnej.md) — §5③ (alokacja), §9 (orkiestracja), D7, D9, D10, D12, D13, D14

---

## 1. Punkt wyjścia

Dziś `LobbyClock.TickAsync` przy `LobbyTick.Started` loguje ostrzeżenie i woła `CloseAndReopen()`.
Zamrożony roster jest **wyrzucany** — nie ma komu go oddać.

Do zbudowania jest wszystko między „lobby dobiło do terminu" a „gracz widzi mapę". Netcode meczu
(protobuf, worker, canvas) jest poza zakresem tego planu; plan kończy się w chwili, gdy klient ma
w ręku adres i bilet.

---

## 2. Przepływ docelowy

```
① Lobby dobija do terminu, roster niepusty
   Lobby.Advance → Starting            ← roster ZAMROŻONY, Join zwraca NotGathering
   zegar rozgłasza nagłówek ze state="Starting" i oddaje snapshot launcherowi

② Launcher (osobny wątek, poza taktem zegara)
   przypisanie slotów        1..N ludzie w kolejności rostera, N+1..MaxActors boty
   zapis Match + Participants                        ← pierwsza rzecz w systemie, która MUSI przetrwać restart
   IMatchAllocator.AllocateAsync                     ← HTTP do agenta, z ponowieniami

③ Bilety — jeden na gracza, nigdy broadcastem
   MatchTicketService.Issue(playerId, matchId, slot)  podpis, TTL 60 s
   Clients.User(playerId).MatchReady { matchId, wsUrl, ticket, expiresAt }

④ Domknięcie lobby
   CurrentLobby.CloseAndReopen()        ← dopiero TERAZ, nie w kroku ①
   gracze w meczu: membershipWanted = false
   pozostali (obserwatorzy): widzą nowe lobby i dołączają, jeśli chcieli

⑤ Klient wchodzi do gry
   MatchGateway zapamiętuje bilet W PAMIĘCI, nawigacja na /match/{matchId}
   wss://gs.example.com/match/{matchId}  →  ClientHello { ticket }
   game-serwer weryfikuje podpis OFFLINE  →  MatchInit + keyframe
```

---

## 3. Co dochodzi w kodzie

### 3.1 Domena i persystencja

Lobby świadomie nie jest encją bazodanową. **Mecz musi nią być** — meta musi umieć odpowiedzieć
„czy gracz X jest uczestnikiem meczu Y na slocie S", i to po restarcie. Bez tego nie da się
ponownie wydać biletu ani przyjąć wyniku.

```
Match                Id (Guid v7), MapId, Mode, MaxActors, Seed,
                     Endpoint (host:port, null do alokacji),
                     State: Allocating | Live | Completed | Failed,
                     CreatedAt, StartedAt?, EndedAt?

MatchParticipant     MatchId, PlayerId, Slot (1..254), Nickname, Color
```

Nick i kolor są **kopiowane** do uczestnika, dokładnie tak jak w `LobbyPlayer` i z tego samego
powodu — plus dwa nowe: game-serwer potrzebuje ich do `SlotInfo` w `MatchInit`, a historia meczów
powinna pokazywać nick używany wtedy, nie dzisiejszy.

**Konwersja koloru wchodzi tutaj.** Protokół (§6) chce `color_rgb`, meta trzyma HSV. Przeliczenie
robi meta przy budowaniu manifestu, żeby C++ nigdy nie dowiedział się o istnieniu HSV.

**Seed generuje meta** (CSPRNG) i zapisuje na `Match`. Bez zapisanego seeda replay z D10 nie
istnieje — log komend bez seeda nie odtworzy niczego.

### 3.2 Przypisanie slotów (D12)

Czysta funkcja, osobno testowalna:

```
0        pustkowie
1..N     ludzie, w kolejności Roster() — czyli JoinedAt, potem PlayerId
N+1..M   boty, M = Map.MaxActors
255      woda
```

Kolejność rostera jest **już** stabilna i to nie przypadek — determinizm (D10) wymaga, żeby
przypisanie dało się odtworzyć. Slot zapisujemy w `MatchParticipant`, nie wyliczamy ponownie:
funkcja może się zmienić, a stare replaye muszą zostać czytelne.

### 3.3 Port alokacji

```
Application/Matches/IMatchAllocator.cs
    Task<MatchAllocation> AllocateAsync(MatchAllocationRequest request, CancellationToken ct)

    MatchAllocationRequest  { MatchId, MapId, MaxActors, Seed }
    MatchAllocation         { Host, Port, WsUrl }
```

`WsUrl` buduje się po regule z D9 — `wss://gs.example.com/match/{matchId}`, jedno wejście na 443,
routing po ścieżce. Klient **nigdy** nie widzi `host:port`; to szczegół sieci wewnętrznej.

| Implementacja | Kiedy |
|---|---|
| `FakeMatchAllocator` | testy i dev bez C++ — zwraca skonfigurowany endpoint |
| `LocalProcessMatchAllocator` | dev z binarką — forkuje proces, wybiera wolny port |
| `AgentMatchAllocator` | produkcja — HTTP do agenta na VM-ce (§9, wariant A) |

### 3.4 Launcher — poza taktem zegara

**To jest najważniejsza decyzja w tym planie.**

Alokacja to I/O sieciowe z ponowieniami. `LobbyClock` awaituje swój tik, więc alokacja wykonana
w tiku **zatrzymuje rozgłaszanie dla całego serwisu** na czas rozmowy z agentem. Dodatkowo
`TickAsync` łapie każdy wyjątek i tylko loguje — nieudana alokacja zniknęłaby bez śladu w stanie.

```
Api/Matches/MatchLauncher.cs : BackgroundService
    Channel<MatchStartRequest>  ←  zegar tylko wpisuje i wraca
```

Zegar zachowuje jedną odpowiedzialność: wykrywa `Started`, oddaje zamrożony snapshot, rozgłasza
nagłówek. Launcher robi resztę i **on** woła `CloseAndReopen`.

Dwie właściwości, które już są w kodzie i o które nie trzeba walczyć:

- **Podwójny start jest niemożliwy.** `Lobby.Advance` pierwszą linią sprawdza
  `State is not LobbyState.Gathering` i zwraca `Idle`. Lobby może siedzieć w `Starting` przez kilka
  sekund alokacji i kolejne tiki nic nie zrobią.
- **Dołączenia w trakcie alokacji są odrzucane** z `NotGathering`, a klient ma już dla tego
  komunikat.

Czego **nie wolno** zrobić: zostawić `CloseAndReopen()` w zegarze. Zamrożony roster musi przeżyć
do momentu, w którym launcher go zużyje.

### 3.5 Dostarczenie biletu — pułapka, którą trzeba nazwać

Bilet jest poświadczeniem na konkretny slot. **Nie może** iść ani przez `Clients.All`, ani przez
grupę `lobby-members`.

Naturalne `Clients.User(playerId)` **nie zadziała bez dopisku**: `MapInboundClaims = false`
w `Program.cs` zostawia tożsamość w claimie `sub`, a domyślny `DefaultUserIdProvider` czyta
`ClaimTypes.NameIdentifier`. Zwróci `null` i wysyłka **po cichu nic nie zrobi** — najgorszy rodzaj
awarii.

Rozwiązanie: własny `IUserIdProvider` czytający `sub` (pięć linii, ta sama logika co
`GetPlayerId`). Daje przy okazji dostarczenie na wszystkie karty gracza. Alternatywa — sięgnięcie
po `connectionOwners` z `CurrentLobby` — działa, ale wystawia wewnętrzne rejestry lobby na zewnątrz.

Nowe wiadomości w `ILobbyClient`:

| Wiadomość | Odbiorcy | Ładunek |
|---|---|---|
| `MatchReady` | **wyłącznie dany gracz** | `{ matchId, wsUrl, ticket, expiresAt }` |
| `MatchStartFailed` | grupa `lobby-members` | `{ reason }` — bez sekretów |

`MatchReady` zostaje minimalny: mapa, tick rate i lista slotów przyjdą w `MatchInit` od
game-serwera, żeby nie mieć dwóch źródeł prawdy. `expiresAt` jest potrzebne, bo klient musi
wiedzieć, kiedy poprosić o świeży bilet.

### 3.6 Bilety — i jedna korekta do dokumentu architektury

Osobny mechanizm od tokenu gracza: tamten jest HS256, bo wystawia i weryfikuje ten sam proces.
Bilet musi weryfikować **game-serwer bez kontaktu z meta** (§4.3), więc podpis jest asymetryczny.
Klucz prywatny w sekretach hosta, publiczny wbudowany w obraz game-serwera.

Claimy zgodnie z §5③: `{ playerId, matchId, slot, nonce }`, TTL 60 s.

> **Do decyzji.** Dokument mówi „Ed25519". Sprawdziłem: **.NET 10 nie ma Ed25519 w BCL** —
> `System.Security.Cryptography` daje `ECDsa` oraz postkwantowe `MLDsa`/`SlhDsa`, Ed25519 nie ma
> wcale. Czyli albo zależność (`NSec.Cryptography` na libsodium, ewentualnie BouncyCastle), albo
> zmiana krzywej.
>
> | Wariant | Za | Przeciw |
> |---|---|---|
> | **Ed25519 przez NSec** | 32 B klucz, 64 B podpis, w C++ trywialny przez libsodium, brak pułapek malleability | natywna zależność na hoście meta |
> | **ECDSA P-256 z BCL** | zero nowych zależności; w C++ przez OpenSSL, który i tak tam będzie | podpis DER ~70 B, więcej ceremonii przy kodowaniu |
>
> Rekomendacja: **ECDSA P-256**, jeśli nie chcemy natywnej biblioteki po stronie .NET — protokół
> jest na to obojętny, a różnica 64 vs 70 bajtów raz na mecz nie znaczy nic. Ed25519 zostaje
> właściwym wyborem, gdy libsodium i tak wejdzie do obrazu z innych powodów. **W obu przypadkach
> dokument architektury wymaga dopisku** — dziś sugeruje, że Ed25519 jest w .NET dostępny.

**Jednorazowość biletu** rozwiązuje się sama dzięki D7: proces obsługuje jeden mecz, więc pamięta
zużyte `nonce` u siebie. Meta nie musi o tym nic wiedzieć.

### 3.7 Ponowne wydanie biletu — nie opcjonalne

```
POST /api/matches/{matchId}/ticket        [Authorize]
  → 200 { ticket, wsUrl, expiresAt }      gdy wołający jest uczestnikiem, a Match.State = Live
  → 404                                    nie jest uczestnikiem albo mecz nie żyje
```

Trzy powody, dla których to wchodzi **razem** z podpisem, a nie później:

1. TTL 60 s. Gracz z zakładką w tle może przegapić `MatchReady`.
2. Dostarczenie do części graczy mogło się nie udać, a proces trzyma ich sloty.
3. To **dokładnie** to, czego wymaga reconnect (D14, punkt 1). Ten sam kod.

### 3.8 Klient

| Element | Rola |
|---|---|
| `MatchGateway` | trzyma bilet **tylko w pamięci** — 60-sekundowe poświadczenie nie ma po co trafiać do `localStorage`, a pamięć umiera razem z kartą, co jest tu zaletą |
| handler `MatchReady` | zapis biletu → `membershipWanted = false` → nawigacja na `/match/{matchId}` |
| trasa `/match/:matchId` | guard: brak biletu → próba ponownego wydania → nadal brak → strona główna |
| handler `MatchStartFailed` | komunikat w widoku lobby; roster odbuduje się sam na nowym lobby |

**Dwie rzeczy dostajemy darmo z już podjętych decyzji:**

- Hub jest wstrzykiwany w `App`, nie w widoku lobby, więc `MatchReady` dojdzie do gracza
  siedzącego na `/profile` albo `/guide`. Nie trzeba nic dokładać.
- Po nieudanym starcie klient wraca do nowego lobby sam — `membershipWanted` plus reakcja na
  zmianę `lobbyId` już to robią.

**Jedna pułapka do domknięcia:** `CloseAndReopen` zmienia `lobbyId`, a klient reaguje na to
ponownym `Join`. Gracz wpuszczony do meczu **nie może** wrócić do kolejki, bo byłby jednocześnie
w meczu i w lobby na następny. Stąd `membershipWanted = false` w handlerze `MatchReady` — kolejność
ma znaczenie, bo nagłówek nowego lobby może przyjść w tej samej sekundzie.

---

## 4. Macierz awarii

| Awaria | Zachowanie | Kto to widzi |
|---|---|---|
| Agent nie odpowiada / brak pojemności | 2–3 szybkie ponowienia, potem `Match.State = Failed` | `MatchStartFailed` → nowe lobby, gracze wracają sami |
| Alokacja udana, część biletów niedostarczona | proces trzyma sloty; gracz dobiera bilet przez §3.7 | nikt, o ile klient poprosi |
| Bilet wygasł przed połączeniem | klient prosi o nowy | nikt |
| Meta restartuje się w trakcie alokacji | `Match` zostaje w `Allocating`; zamiatanie po starcie oznacza go jako `Failed` | lobby i tak jest nowe po restarcie (stan w pamięci) |
| Game-serwer wstał, ale nikt się nie połączył | proces gasi się po timeoucie (reaping, §9) | — |
| Gracz w rosterze, ale rozłączony w chwili startu | **decyzja otwarta**, patrz §6.3 | — |

Ciepła pula procesów (§9) nie jest tu wymagana, ale zamienia „nieudany start" w zdarzenie
rzadkie — warto ją mieć, zanim wpuścimy prawdziwych graczy.

---

## 5. Kolejność wprowadzania

**Etap 1 — bez C++ i bez kryptografii.** `Match` + `MatchParticipant` + migracja, przypisanie
slotów, `IMatchAllocator` z atrapą, `MatchLauncher` na kanale, `MatchReady` / `MatchStartFailed`,
własny `IUserIdProvider`, `membershipWanted = false` po `MatchReady`, włączenie
`Lobby:CountdownEnabled`. Bilet może być na tym etapie nieprzezroczystym stringiem bez podpisu.

Cały etap jest **w pełni testowalny bez jednej linii C++** — i to jest główny argument, żeby zrobić
go pierwszym. Po nim widać na ekranie, że lobby startuje, wypuszcza graczy i otwiera następne.

**Etap 2 — bilet i wejście.** Podpis (§3.6), endpoint ponownego wydania, trasa `/match/:matchId`
z guardem i zaślepką zamiast mapy. Klient realnie „wchodzi do gry", tylko gra jest jeszcze pusta.

**Etap 3 — prawdziwa alokacja.** `LocalProcessMatchAllocator`, potem `AgentMatchAllocator`, ciepła
pula, reaping.

**Etap 4 — domknięcie pętli.** `Internal/` na mTLS, odbiór wyniku meczu (idempotentnie po
`matchId`), zapis do `Match`. Reconnect (D14) jest po etapie 2 prawie darmowy.

---

## 6. Decyzje do podjęcia przed kodowaniem

**6.1 Krzywa podpisu biletu.** Patrz §3.6. Blokuje etap 2, nie blokuje etapu 1.

**6.2 Czy `MaxActors` rozdzielić.** Dziś jedno pole — sufit ludzi i botów razem (mapa `moon`: 100).
Dokument mówi o 10–64 ludziach przy 100–254 aktorach, a komentarz w `MapDefinition` już to
przewiduje. Jeśli sufit ludzi ma być niższy, dochodzi drugie pole i `IsFull` liczy z niego.
Decyzja gameplayowa.

**6.3 Gracz rozłączony w chwili startu.** Dostaje slot i mecz na niego czeka (D14 punkt 2 mówi
o trzymaniu slotu 60–120 s), czy jest pomijany przy zamrażaniu? Rekomendacja: **dostaje slot** —
karencja 5 s i tak trzyma go w rosterze, więc pominięcie dotyczyłoby tylko naprawdę
rozłączonych, a odróżnienie „odświeża stronę" od „zamknął kartę" jest niepewne. Wiąże się z D14
punkt 3 (co robi terytorium nieobecnego gracza), który jest decyzją gameplayową i nie blokuje
etapu 1.

**6.4 `mapSha256` i ścieżka CDN.** `MapDefinition` ich nie ma, a `MatchInit` (§6) i adresowanie
hashem (D13) ich wymagają. Dochodzą razem z pierwszym prawdziwym plikiem mapy — czyli w etapie 3,
nie wcześniej.

**6.5 Czy `Starting` wchodzi przed alokacją.** Rekomendacja: **tak**, zamrożenie rostera musi
poprzedzić I/O — inaczej ktoś dołączy w trakcie alokacji i nie dostanie slotu. Konsekwencja: gracze
widzą `state="Starting"` na kilka sekund i UI powinien to pokazać („przydzielanie serwera…")
zamiast zostawiać licznik na zerze.
