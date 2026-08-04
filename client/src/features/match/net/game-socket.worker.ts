/**
 * Worker meczu: gniazdo, protobuf, teren, tablica właścicieli i rysowanie.
 *
 * **Wszystko, co duże, jest tutaj.** Wątek główny oddaje kanwę przez
 * `transferControlToOffscreen()` i od tej chwili nigdy nie dotyka ani terenu (2 MB), ani
 * `owner[]` (2 MB), ani bufora pikseli (8 MB). Wraca do niego kilkaset bajtów stanu paska.
 *
 * Dwie pętle są rozdzielone celowo (§4.1 dokumentu architektury): sieć chodzi swoim
 * rytmem 5 Hz, rysowanie klatkami. Konsekwencja jest poprawnościowa, nie wydajnościowa —
 * w karcie w tle przeglądarka dławi `requestAnimationFrame` praktycznie do zera, ale
 * worker i WebSocket działają dalej, więc stan przychodzi na bieżąco i obraz wraca przy
 * pierwszej klatce po powrocie do karty.
 */
/* eslint-disable @typescript-eslint/no-explicit-any */
import { create, fromBinary, toBinary } from '@bufbuild/protobuf';

import { formatPopulation } from '../../../core/format';
import { ClientMsgSchema, ServerMsgSchema } from '../../../proto/game_pb';
import { applyOwnershipDeltas, DeltaGroup } from './deltas';
import { decodeTmap, sha256Hex, toHex } from '../map/tmap';
import { Camera } from '../render/camera';
import { MapLabel, MapRenderer } from '../render/map-renderer';
import { buildPalette } from '../render/palette';
import { measureTerritories, Territory } from '../render/territories';
import { FromWorker, SlotView, ToWorker, Viewport } from './worker-protocol';

/**
 * Zasięg workera opisany lokalnie, zamiast przez `/// <reference lib="webworker" />`.
 *
 * Ta dyrektywa dociąga bibliotekę typów WebWorkera do **całego** programu, a ten jest
 * budowany z biblioteką DOM — obie deklarują te same nazwy inaczej i kompilacja kończy się
 * konfliktami w plikach, które z workerem nie mają nic wspólnego. Trzy linijki niżej
 * kosztują mniej niż osobny `tsconfig.worker.json`.
 */
declare const self: {
  postMessage(message: FromWorker): void;
  addEventListener(type: 'message', listener: (event: MessageEvent<ToWorker>) => void): void;
};

/** Odstęp między pingami aplikacyjnymi. Przeglądarka nie daje dostępu do ramek protokołu. */
const PING_INTERVAL_MS = 5000;

/** Ile czekamy przed ponownym połączeniem. Krótko — bilet i tak żyje minutę. */
const RECONNECT_DELAY_MS = 1000;

/**
 * Po tylu nieudanych próbach z rzędu uznajemy, że po drugiej stronie nikogo nie ma.
 *
 * Nie jest to ostrożność, tylko warunek konieczny: meta uważa mecz za żywy, dopóki nie
 * odbierze jego wyniku, a tego odbioru jeszcze nie ma. Bez tego progu gracz, którego proces
 * meczu padł, dobijałby się w nieskończoność do adresu, pod którym nikt nie nasłuchuje —
 * i nie mógłby wyjść, bo mecz jest stanem wyłącznym.
 */
const DEAD_AFTER_ATTEMPTS = 5;

function send(message: FromWorker) {
  self.postMessage(message);
}

/** Woda w tablicy właścicieli (D12). Nie ma czego na niej atakować. */
const WATER_OWNER = 255;

/**
 * Ile kafelków ma objąć kadr po wejściu do meczu.
 *
 * Terytorium startowe to romb o promieniu ośmiu kafelków, więc taki kadr pokazuje je
 * w całości razem z zapasem pustkowia dookoła — czyli dokładnie to, na co gracz zaraz
 * ruszy. Ciaśniej znaczyłoby nawigowanie na ślepo, szerzej — szukanie siebie na mapie.
 */
const START_TILES_VISIBLE = 60;

/**
 * Co ile porcji stanu publicznego przeliczane są pozycje podpisów.
 *
 * Populacje na podpisach odświeżają się co sekundę razem z `PublicState`, ale **miejsce**,
 * w którym stoją, wymaga przejścia po całej tablicy właścicieli. Państwa nie skaczą po
 * mapie, więc raz na trzy sekundy wystarczy — a przy dwóch milionach kafelków to jest
 * różnica między kosztem niezauważalnym a widocznym w klatkach.
 */
const LABEL_REFRESH_EVERY = 3;

/**
 * Ile ekranów na sekundę przejeżdża kamera na klawiszach.
 *
 * Mierzone wysokością kadru, a nie w pikselach: na każdym ekranie i przy każdym zoomie ruch
 * ma być tak samo szybki **w tym, co widać**, bo tak go odbiera ręka.
 */
const KEY_PAN_SCREENS_PER_SECOND = 0.9;

/**
 * Czas dojścia do pełnej prędkości — i tyle samo do zatrzymania.
 *
 * Bez rozbiegu klawisz szarpie kamerą z miejsca i przy krótkim wciśnięciu widok przeskakuje
 * zamiast drgnąć. Dziesiąta sekundy jest poniżej progu, na którym ruch czyta się jako
 * opóźniony, i wystarcza, żeby start i stop były gładkie.
 */
const KEY_PAN_RAMP_MS = 110;

/** Powód odrzucenia rozkazu po ludzku. Numery są z `RejectReason` w `game.proto`. */
function describeRejection(reason: number): string {
  switch (reason) {
    case 1:
      return 'Nie ma czego atakować w tym miejscu.';
    case 2:
      return 'Za mało złota.';
    case 3:
      return 'Nie masz wspólnej granicy z tym graczem.';
    case 4:
      return 'Serwer nie rozpoznał rozkazu.';
    default:
      return 'Rozkaz odrzucony.';
  }
}

class MatchSession {
  private socket: WebSocket | null = null;
  private ticket: string;
  private readonly url: string;

  private canvas: OffscreenCanvas;
  private viewport: Viewport;

  private camera: Camera | null = null;
  private renderer: MapRenderer | null = null;

  /** Właściciel każdego kafelka; indeks jak w `MatchInit` — wiersz po wierszu. */
  private owner: Uint8Array | null = null;

  private terrain: Uint8Array | null = null;
  private palette: Uint32Array | null = null;

  private width = 0;
  private height = 0;

  /** Slot tego gracza. Worker potrzebuje go, żeby wiedzieć, czego nie atakować i co wyśrodkować. */
  private yourSlot = 0;

  /** Obsada meczu — worker trzyma ją dla nicków na podpisach terytoriów. */
  private slots: readonly SlotView[] = [];

  /** Ludzie w puli każdego slotu; z `PublicState`, więc odświeżani raz na sekundę. */
  private populations = new Uint32Array(256);

  /** Kafelki każdego slotu z ostatniego `PublicState` — wielkość pisma podpisu. */
  private territoryTiles = new Uint32Array(256);

  /** Ostatni pomiar terytoriów: kotwice podpisów i prostokąty do wyśrodkowania kadru. */
  private territories: readonly (Territory | null)[] = [];

  /** Ile porcji stanu publicznego przyszło — patrz {@link LABEL_REFRESH_EVERY}. */
  private publicStates = 0;

  /** Czy kamera stanęła już na terytorium gracza. Ustawiane raz — patrz {@link focusOnOwnLand}. */
  private focused = false;

  /** Kierunek przesuwania z klawiatury i bieżąca prędkość w pikselach na sekundę. */
  private keyX = 0;
  private keyY = 0;
  private panVelocityX = 0;
  private panVelocityY = 0;

  /** Czas poprzedniej klatki; `0` znaczy „pierwsza od startu". */
  private lastFrameAt = 0;

  /** Pikseli urządzenia na piksel CSS — do progów czytelności podpisów. */
  private ratio = 1;

  /** Numer kolejnego rozkazu. Wraca w `CommandRejected`, więc musi rosnąć. */
  private commandSeq = 0;

  private stopped = false;
  private dirty = false;

  /** Nieudane próby połączenia z rzędu. Zerowane przez każde udane otwarcie gniazda. */
  private failures = 0;
  private pingTimer: ReturnType<typeof setInterval> | null = null;
  private pingSentAt = 0;

  constructor(
    canvas: OffscreenCanvas,
    url: string,
    ticket: string,
    viewport: Viewport,
    ratio: number,
  ) {
    this.canvas = canvas;
    this.url = url;
    this.ticket = ticket;
    this.viewport = viewport;
    this.ratio = ratio;

    this.canvas.width = viewport.width;
    this.canvas.height = viewport.height;

    this.connect();
  }

  // ─────────────────────────────── gniazdo ───────────────────────────────

  private connect() {
    if (this.stopped) {
      return;
    }

    send({ type: 'link', state: this.owner ? 'reconnecting' : 'connecting' });

    const socket = new WebSocket(this.url);
    socket.binaryType = 'arraybuffer';

    this.socket = socket;

    socket.addEventListener('open', () => {
      socket.send(
        toBinary(
          ClientMsgSchema,
          create(ClientMsgSchema, { msg: { case: 'hello', value: { ticket: this.ticket } } }),
        ),
      );

      this.failures = 0;

      send({ type: 'link', state: 'live' });

      this.pingTimer = setInterval(() => this.ping(), PING_INTERVAL_MS);
    });

    socket.addEventListener('message', (event) => this.onMessage(event.data as ArrayBuffer));

    socket.addEventListener('close', (event) => this.onClose(event.code, event.reason));
  }

  private onClose(code: number, reason: string) {
    if (this.pingTimer !== null) {
      clearInterval(this.pingTimer);
      this.pingTimer = null;
    }

    if (this.stopped) {
      return;
    }

    // 1008 to odrzucony bilet — jedyny powód, dla którego ponowne łączenie z tym samym
    // poświadczeniem nie ma sensu. Wątek główny ma dostęp do HTTP i dobierze nowy.
    if (code === 1008) {
      send({ type: 'link', state: 'reconnecting', detail: reason || 'bilet odrzucony' });
      send({ type: 'need-ticket' });

      return;
    }

    if (code === 1000) {
      send({ type: 'link', state: 'closed', detail: 'mecz zakończony' });

      return;
    }

    this.failures += 1;

    if (this.failures >= DEAD_AFTER_ATTEMPTS) {
      send({
        type: 'link',
        state: 'closed',
        detail: `serwer meczu nie odpowiada (${this.failures} próby)`,
      });

      return;
    }

    send({ type: 'link', state: 'reconnecting', detail: `połączenie zerwane (${code})` });

    setTimeout(() => this.connect(), RECONNECT_DELAY_MS);
  }

  private ping() {
    if (this.socket?.readyState !== WebSocket.OPEN) {
      return;
    }

    this.pingSentAt = Date.now();

    this.socket.send(
      toBinary(
        ClientMsgSchema,
        create(ClientMsgSchema, {
          msg: { case: 'ping', value: { clientTimeMs: BigInt(this.pingSentAt) } },
        }),
      ),
    );
  }

  /** Nowy bilet z wątku głównego — łączymy się natychmiast, bez czekania. */
  renew(ticket: string) {
    this.ticket = ticket;

    this.connect();
  }

  // ─────────────────────────────── wiadomości ───────────────────────────────

  private onMessage(payload: ArrayBuffer) {
    const message = fromBinary(ServerMsgSchema, new Uint8Array(payload));

    switch (message.msg.case) {
      case 'init':
        this.onInit(message.msg.value, payload.byteLength);
        break;

      case 'snapshot':
        this.onSnapshot(message.msg.value, payload.byteLength);
        break;

      case 'pong':
        send({ type: 'rtt', milliseconds: Date.now() - this.pingSentAt });
        break;

      case 'myState': {
        const state = message.msg.value;

        send({
          type: 'own-state',
          state: {
            population: state.population,
            maxPopulation: state.maxPopulation,
            popIncome: state.popIncome,
            gold: state.gold,
            goldIncome: state.goldIncome,
            attackForce: state.attackForce,
            cities: state.cities,
            nextCityCost: state.nextCityCost,
            taxAmount: state.taxAmount,
            taxInMs: state.taxInMs,
            taxPeriodMs: state.taxPeriodMs,
          },
        });

        break;
      }

      case 'rejected':
        send({ type: 'order-rejected', detail: describeRejection(message.msg.value.reason) });

        break;

      case 'end':
        send({ type: 'link', state: 'closed', detail: 'mecz zakończony' });
        break;

      default:
        break;
    }
  }

  private onInit(init: any, _bytes: number) {
    const slots: SlotView[] = init.slots.map((slot: any) => ({
      slot: slot.slot,
      name: slot.name,
      colorRgb: slot.colorRgb,
      isBot: slot.isBot,
    }));

    this.width = init.mapWidth;
    this.height = init.mapHeight;
    this.yourSlot = init.yourSlot;
    this.slots = slots;
    this.palette = buildPalette(slots);
    this.owner = new Uint8Array(this.width * this.height);

    send({
      type: 'init',
      mapId: init.mapId,
      width: this.width,
      height: this.height,
      yourSlot: init.yourSlot,
      tickRate: init.tickRate,
      slots,
    });

    // Teren pobierany dopiero teraz, bo dopiero teraz znamy jego sumę kontrolną — a ona
    // jest częścią adresu. Po reconnekcie mamy go już w pamięci i nie ruszamy sieci.
    if (!this.terrain) {
      void this.loadTerrain(init.mapId, toHex(init.mapSha256));
    } else {
      this.rebuild();
    }
  }

  private onSnapshot(snapshot: any, bytes: number) {
    send({ type: 'tick', tick: snapshot.tick });

    if (snapshot.isKeyframe && this.owner) {
      this.applyKeyframe(snapshot.runs, bytes);
    } else if (snapshot.deltas.length > 0 && this.owner) {
      this.applyDeltas(snapshot.deltas);
    }

    if (snapshot.others.length > 0) {
      this.onPublicState(snapshot.others);
    }
  }

  /**
   * Stan publiczny: ranking do paska i podpisy na mapę.
   *
   * Populacje idą tędy dla **wszystkich** slotów, a nie tylko dla dziesiątki z rankingu:
   * podpis dostaje każde widoczne państwo, a ranking jest tylko wycinkiem tej samej listy.
   */
  private onPublicState(others: readonly any[]) {
    this.populations.fill(0);
    this.territoryTiles.fill(0);

    for (const state of others) {
      this.populations[state.slot] = state.population;
      this.territoryTiles[state.slot] = state.territoryTiles;
    }

    send({
      type: 'standings',
      entries: others
        .map((state: any) => ({ slot: state.slot, tiles: state.territoryTiles }))
        .sort((a: any, b: any) => b.tiles - a.tiles)
        .slice(0, 10),
    });

    if (this.publicStates++ % LABEL_REFRESH_EVERY === 0) {
      this.measure();
    }

    this.refreshLabels();
  }

  /** Przechodzi po tablicy właścicieli i zapamiętuje, gdzie stoją terytoria. */
  private measure() {
    if (!this.owner) {
      return;
    }

    this.territories = measureTerritories(this.owner, this.width, this.height);
  }

  /**
   * Składa podpisy terytoriów z dwóch źródeł: miejsca z pomiaru i liczb z ostatniego
   * `PublicState`.
   *
   * Liczba kafelków bierze się ze stanu publicznego, a nie z pomiaru, bo jest o dwie sekundy
   * świeższa — a to od niej zależy wielkość pisma, czyli to, czy podpis w ogóle się pojawi.
   */
  private refreshLabels() {
    if (this.territories.length === 0) {
      return;
    }

    const labels: MapLabel[] = [];

    for (const slot of this.slots) {
      const territory = this.territories[slot.slot];
      const tiles = this.territoryTiles[slot.slot];

      // Zero kafelków znaczy „wykreślony albo jeszcze nie było o nim stanu publicznego".
      // W obu przypadkach podpis byłby kłamstwem — w drugim wypisałby zerową populację
      // państwa, które ma się dobrze.
      if (!territory || tiles === 0) {
        continue;
      }

      labels.push({
        // Pół kafelka, żeby podpis stanął na jego środku, a nie w lewym górnym rogu.
        x: territory.anchorX + 0.5,
        y: territory.anchorY + 0.5,
        name: slot.name,
        people: formatPopulation(this.populations[slot.slot]),
        tiles,
      });
    }

    this.renderer?.setLabels(labels);
    this.dirty = true;
  }

  /**
   * Odtwarza `owner[]` z runów.
   *
   * Keyframe **nie wypisuje pustkowi** — opisuje je sama ich nieobecność, więc tablicę
   * trzeba wyzerować, zanim runy zaczną ją wypełniać. `start_delta` jest długością przerwy
   * od końca poprzedniego runu, a nie zawsze zerem.
   */
  private applyKeyframe(runs: readonly any[], bytes: number) {
    const owner = this.owner;

    if (!owner) {
      return;
    }

    owner.fill(0);

    let cursor = 0;
    let covered = 0;

    for (const run of runs) {
      cursor += run.startDelta;

      owner.fill(run.slot, cursor, cursor + run.length);

      cursor += run.length;
      covered += run.length;
    }

    send({ type: 'map', runs: runs.length, tiles: covered, bytes });

    this.rebuild();
  }

  /**
   * Nanosi kafelki, które zmieniły właściciela.
   *
   * Przemalowanie idzie przez `paintTiles`, nie przez `paintAll`: całą mapę przepisuje się
   * w dwóch milionach kroków, a delt jest zwykle kilkadziesiąt.
   *
   * Tablica właścicieli aktualizuje się **także wtedy, gdy renderera jeszcze nie ma** —
   * teren pobiera się z sieci i przez pierwsze sekundy meczu go nie było. Delty pominięte
   * w tym oknie byłyby stracone do najbliższego keyframe'a, czyli do reconnectu, a mapa
   * pokazywałaby stan sprzed nich. `rebuild()` po dojściu terenu przepisze wszystko z tej
   * samej tablicy.
   */
  private applyDeltas(groups: readonly DeltaGroup[]) {
    const owner = this.owner;

    if (!owner) {
      return;
    }

    const touched = applyOwnershipDeltas(groups, owner);

    this.renderer?.paintTiles(touched, owner);
    this.dirty = true;
  }

  // ─────────────────────────────── teren ───────────────────────────────

  private async loadTerrain(mapId: string, sha256: string) {
    send({ type: 'terrain', state: 'loading' });

    try {
      // Hash w ścieżce jest kluczem cache'a: plik pod tym adresem nigdy się nie zmieni,
      // więc przeglądarka trzyma go rok i po powrocie do meczu nie rusza sieci.
      const response = await fetch(`/maps/${mapId}/${sha256}/terrain.bin`);

      if (!response.ok) {
        throw new Error(`serwer odpowiedział ${response.status}`);
      }

      const buffer = await response.arrayBuffer();

      // Porównanie sum kontrolnych jest **jedynym** powodem, dla którego `mapSha256`
      // istnieje w protokole (D13): teren z cache'a albo z podmienionego CDN-u nie ma
      // prawa cicho różnić się od tego, na którym mecz naprawdę się toczy.
      const actual = await sha256Hex(buffer);

      if (actual !== sha256) {
        throw new Error(`suma kontrolna terenu to ${actual.slice(0, 12)}…, a mecz gra na ${sha256.slice(0, 12)}…`);
      }

      const map = decodeTmap(buffer);

      if (map.width !== this.width || map.height !== this.height) {
        throw new Error(
          `teren ma ${map.width}×${map.height}, a mecz ${this.width}×${this.height}`,
        );
      }

      this.terrain = map.terrain;

      send({ type: 'terrain', state: 'ready' });

      this.rebuild();
    } catch (error) {
      send({
        type: 'terrain',
        state: 'error',
        detail: error instanceof Error ? error.message : String(error),
      });
    }
  }

  // ─────────────────────────────── rysowanie ───────────────────────────────

  /** Składa renderer, gdy ma już wszystkie trzy składniki: teren, paletę i właścicieli. */
  private rebuild() {
    if (!this.terrain || !this.palette || !this.owner) {
      return;
    }

    // Wyjątek stąd zabiłby workera po cichu, a gracz zostałby z czarnym prostokątem
    // i paskiem stanu mówiącym „live". Czarny ekran bez wyjaśnienia to najgorsza możliwa
    // awaria tego widoku — jedyna, której gracz nie umie ani zrozumieć, ani obejść.
    try {
      if (!this.renderer) {
        this.renderer = new MapRenderer(
          this.canvas,
          this.width,
          this.height,
          this.terrain,
          this.palette,
        );

        this.camera = new Camera(this.width, this.height, this.viewport);
      } else {
        this.renderer.setPalette(this.palette);
      }

      // Slot ustawiany przy każdym składaniu, nie tylko przy pierwszym: renderer powstaje
      // dopiero po dojściu terenu, więc w chwili `MatchInit` jeszcze go nie ma.
      this.renderer.setViewerSlot(this.yourSlot);
      this.renderer.setPixelRatio(this.ratio);

      this.renderer.paintAll(this.owner);

      // Pomiar od razu po pełnym stanie mapy: bez niego przycisk „wyśrodkuj" przez pierwszą
      // sekundę meczu nie miałby czego wyśrodkować.
      this.measure();
      this.refreshLabels();

      this.focusOnOwnLand();

      this.dirty = true;
    } catch (error) {
      send({
        type: 'fatal',
        detail: error instanceof Error ? error.message : String(error),
      });
    }
  }

  resize(viewport: Viewport, ratio: number) {
    this.viewport = viewport;
    this.ratio = ratio;

    this.renderer?.setPixelRatio(ratio);

    if (this.renderer && this.camera) {
      this.renderer.resize(viewport.width, viewport.height);
      this.camera.resize(viewport);
    } else {
      this.canvas.width = viewport.width;
      this.canvas.height = viewport.height;
    }

    this.dirty = true;
  }

  /**
   * Ustawia kadr na terytorium gracza — raz, po pierwszym pełnym stanie mapy.
   *
   * Środek bierze się z **pomiaru własnych kafelków**, a nie z punktu startowego: ten drugi
   * nie jest workerowi znany, a po reconnekcie w połowie meczu byłby i tak nieaktualny.
   * Jednorazowość jest tu istotna — kolejne keyframe'y przychodzą przy każdym powrocie do
   * meczu, a szarpnięcie kamerą w trakcie rozgrywki jest gorsze niż jej brak.
   */
  private focusOnOwnLand() {
    const own = this.territories[this.yourSlot];

    if (this.focused || !this.camera || this.yourSlot === 0 || !own) {
      return;
    }

    this.camera.focus(own.anchorX, own.anchorY, START_TILES_VISIBLE);

    this.focused = true;
  }

  /**
   * Kadr na całe terytorium wskazanego slotu — obsługa przycisków wyśrodkowania.
   *
   * Pomiar jest odświeżany na miejscu, zamiast brany z ostatniego przebiegu: kliknięcie
   * zdarza się kilka razy na mecz, a kadr rozjechany o trzy sekundy ekspansji nie obejmuje
   * tego, co przycisk obiecuje.
   */
  focusOn(slot: number) {
    if (!this.camera) {
      return;
    }

    this.measure();
    this.refreshLabels();

    const territory = this.territories[slot];

    if (!territory) {
      return;
    }

    this.camera.fit(territory.minX, territory.minY, territory.maxX, territory.maxY);

    this.dirty = true;
  }

  pan(dx: number, dy: number) {
    this.camera?.panBy(dx, dy);

    this.dirty = true;
  }

  /** Zapamiętuje kierunek z klawiatury; ruch liczy {@link frame}. */
  panKeys(dx: number, dy: number) {
    this.keyX = Math.sign(dx);
    this.keyY = Math.sign(dy);
  }

  /**
   * Przesuwa kamerę zgodnie z wciśniętymi klawiszami.
   *
   * Prędkość dochodzi do docelowej wykładniczo, więc start i zatrzymanie są gładkie, a ruch
   * mierzony jest **czasem klatki**, nie ich liczbą: na monitorze 144 Hz kamera ma jechać
   * tak samo szybko jak na sześćdziesiątce.
   *
   * @returns `true`, jeśli kamera się w tej klatce ruszyła.
   */
  private applyKeyPan(seconds: number): boolean {
    const target = KEY_PAN_SCREENS_PER_SECOND * this.viewport.height;

    // Po skosie tak samo szybko jak w pionie — bez tego przekątna jest o 41% szybsza,
    // co po klawiaturze czyta się jak przyspieszanie w losowych momentach.
    const diagonal = this.keyX !== 0 && this.keyY !== 0 ? Math.SQRT1_2 : 1;

    const ramp = Math.min(1, seconds / (KEY_PAN_RAMP_MS / 1000));

    this.panVelocityX += (this.keyX * target * diagonal - this.panVelocityX) * ramp;
    this.panVelocityY += (this.keyY * target * diagonal - this.panVelocityY) * ramp;

    // Poniżej piksela na sekundę ruchu nie widać, a niezerowa prędkość trzymałaby pętlę
    // rysowania w gotowości do końca meczu.
    if (Math.abs(this.panVelocityX) < 1 && Math.abs(this.panVelocityY) < 1) {
      this.panVelocityX = 0;
      this.panVelocityY = 0;

      return false;
    }

    // Znak odwrotny do przeciągania: „w prawo" znaczy, że w prawo idzie kamera, a nie mapa.
    this.camera?.panBy(-this.panVelocityX * seconds, -this.panVelocityY * seconds);

    return true;
  }

  zoom(factor: number, x: number, y: number) {
    this.camera?.zoomAt(factor, x, y);

    this.dirty = true;
  }

  /**
   * Atak na właściciela kafelka pod wskazanym punktem ekranu.
   *
   * Cel wyznacza worker, bo tylko on ma kamerę i tablicę właścicieli. Kliknięcie w wodę
   * albo we własne terytorium jest **milczące** — to nie jest pomyłka warta komunikatu,
   * tylko chybione kliknięcie, a serwer i tak odrzuciłby taki rozkaz.
   */
  attackAt(x: number, y: number, percent: number) {
    if (!this.camera || !this.owner) {
      return;
    }

    const tile = this.camera.toTile(x, y);

    const tileX = Math.floor(tile.x);
    const tileY = Math.floor(tile.y);

    if (tileX < 0 || tileY < 0 || tileX >= this.width || tileY >= this.height) {
      return;
    }

    const target = this.owner[tileY * this.width + tileX];

    if (target === WATER_OWNER || target === this.yourSlot) {
      return;
    }

    this.command({ case: 'attack', value: { targetSlot: target, populationPct: percent } });
  }

  /** `tile_index` jest ignorowany po stronie serwera: miasta nie stoją jeszcze na mapie. */
  buildCity() {
    this.command({ case: 'build', value: { tileIndex: 0 } });
  }

  private command(order: { case: 'attack' | 'build'; value: object }) {
    if (this.socket?.readyState !== WebSocket.OPEN) {
      return;
    }

    this.commandSeq += 1;

    this.socket.send(
      toBinary(
        ClientMsgSchema,
        create(ClientMsgSchema, {
          msg: { case: 'command', value: { seq: this.commandSeq, order } },
        }),
      ),
    );
  }

  /**
   * Klatka.
   *
   * Rysujemy **tylko gdy coś się zmieniło** — wątek główny woła to sześćdziesiąt razy na
   * sekundę, a mapa bez ruchu kamery i bez nowego stanu wygląda tak samo. Flaga zdejmuje
   * przy okazji zdwojenie: seria zdarzeń wskaźnika w jednej klatce daje jedno rysowanie.
   */
  frame() {
    const now = performance.now();

    // Czas klatki obcięty do ćwierć sekundy: karta wraca z tła po minutach, a kamera
    // z wciśniętym klawiszem przeskoczyłaby wtedy pół mapy jednym krokiem.
    const seconds = this.lastFrameAt === 0 ? 0 : Math.min(0.25, (now - this.lastFrameAt) / 1000);

    this.lastFrameAt = now;

    if (!this.renderer || !this.camera) {
      return;
    }

    if (seconds > 0 && this.applyKeyPan(seconds)) {
      this.dirty = true;
    }

    // Animacja przed sprawdzeniem flagi, nie po: gasnące błyski zmieniają bitmapę mapy
    // same z siebie, bez nowego snapshotu i bez ruchu kamery. Warunek oparty wyłącznie na
    // `dirty` zatrzymywałby je w pół drogi na nieruchomym ekranie.
    const animating = this.renderer.animate(now);

    if (!this.dirty && !animating) {
      return;
    }

    this.dirty = false;

    this.renderer.draw(this.camera);
  }

  stop() {
    this.stopped = true;

    if (this.pingTimer !== null) {
      clearInterval(this.pingTimer);
    }

    this.socket?.close(1000, 'gracz wyszedł');
  }
}

let session: MatchSession | null = null;

self.addEventListener('message', (event) => {
  const message = event.data;

  switch (message.type) {
    case 'start':
      session = new MatchSession(
        message.canvas,
        message.url,
        message.ticket,
        message.viewport,
        message.ratio,
      );
      break;

    case 'ticket':
      session?.renew(message.ticket);
      break;

    case 'viewport':
      session?.resize(message.viewport, message.ratio);
      break;

    case 'pan':
      session?.pan(message.dx, message.dy);
      break;

    case 'pan-keys':
      session?.panKeys(message.dx, message.dy);
      break;

    case 'focus':
      session?.focusOn(message.slot);
      break;

    case 'zoom':
      session?.zoom(message.factor, message.x, message.y);
      break;

    case 'attack-at':
      session?.attackAt(message.x, message.y, message.percent);
      break;

    case 'build-city':
      session?.buildCity();
      break;

    case 'frame':
      session?.frame();
      break;

    case 'stop':
      session?.stop();
      session = null;
      break;
  }
});
