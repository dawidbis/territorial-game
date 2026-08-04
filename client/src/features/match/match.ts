import {
  Component,
  computed,
  DestroyRef,
  effect,
  ElementRef,
  inject,
  signal,
  untracked,
  viewChild,
} from '@angular/core';
import { Router } from '@angular/router';

import { formatCompact, formatPopulation } from '../../core/format';
import { MatchGateway } from '../../core/services/match-gateway';
import {
  FromWorker,
  LinkState,
  OwnState,
  SlotView,
  StandingView,
  ToWorker,
} from './net/worker-protocol';

/**
 * Widok meczu — kanwa, pasek stanu i nic więcej.
 *
 * Ten komponent **nie renderuje mapy i nigdy nie zobaczy jej danych**. Oddaje kanwę
 * workerowi przez `transferControlToOffscreen()`, przekazuje mu zdarzenia wskaźnika
 * i wyświetla kilkaset bajtów stanu, które wracają. Podział jest z §4.1 dokumentu
 * architektury i obowiązuje od pierwszej linii, bo przeniesienie renderowania do workera
 * po fakcie jest przepisaniem, nie refaktorem.
 */
@Component({
  selector: 'app-match',
  templateUrl: './match.html',
  // Klawiatura słuchana na oknie, a nie na kanwie: kanwa nie jest elementem, który da się
  // ustawić w fokusie, a wymuszanie tego przez `tabindex` znaczyłoby, że sterowanie mapą
  // przestaje działać po każdym kliknięciu w przycisk paska.
  host: {
    '(window:keydown)': 'onKeyDown($event)',
    '(window:keyup)': 'onKeyUp($event)',
    '(window:blur)': 'releaseKeys()',
  },
})
export class Match {
  /** Powyżej tylu pikseli ruchu wskaźnik przeciąga mapę, zamiast wydawać rozkaz. */
  private static readonly dragThresholdPixels = 6;

  /**
   * Klawisze przesuwania kamery i ich kierunki.
   *
   * WSAD i strzałki obok siebie, bo obie konwencje są w tym gatunku żywe, a wybór między
   * nimi nie jest wart ani ustawienia, ani zgadywania.
   */
  private static readonly panKeys = new Map<string, readonly [number, number]>([
    ['w', [0, -1]],
    ['s', [0, 1]],
    ['a', [-1, 0]],
    ['d', [1, 0]],
    ['arrowup', [0, -1]],
    ['arrowdown', [0, 1]],
    ['arrowleft', [-1, 0]],
    ['arrowright', [1, 0]],
  ]);

  private router = inject(Router);
  private destroyRef = inject(DestroyRef);

  protected matches = inject(MatchGateway);

  private surface = viewChild<ElementRef<HTMLCanvasElement>>('surface');

  private worker: Worker | null = null;
  private frameHandle = 0;
  private dragPointer: number | null = null;
  private dragX = 0;
  private dragY = 0;

  /**
   * Ile pikseli przejechał wskaźnik od wciśnięcia.
   *
   * Kliknięcie i przeciąganie zaczynają się identycznie, a znaczą co innego: jedno wysyła
   * armię, drugie przesuwa widok. Rozstrzyga dopiero dystans — bez progu każde przesunięcie
   * mapy kończyłoby się przypadkowym atakiem.
   */
  private dragDistance = 0;

  /**
   * Ile procent puli idzie w jednym rozkazie. Wartość suwaka w dolnym pasku.
   *
   * Dwadzieścia procent, bo tyle wychodzi z samego przyrostu w kilkanaście sekund: rozkaz
   * przy tym ustawieniu jest zaczepką, a nie decyzją o losie państwa. Połowa puli w domyśle
   * uczyła czegoś odwrotnego — pierwszy odruchowy klik zabierał połowę armii.
   */
  protected sendPercent = signal(20);

  /** Wciśnięte klawisze przesuwania. Zbiór, bo skos wymaga dwóch naraz. */
  private held = new Set<string>();

  protected leaving = signal(false);
  protected problem = signal<string | null>(null);

  /** `false` wyłącznie na przeglądarkach bez `OffscreenCanvas` — patrz {@link start}. */
  protected supported = signal(true);

  protected link = signal<LinkState>('connecting');
  protected linkDetail = signal<string | null>(null);
  protected terrain = signal<'loading' | 'ready' | 'error'>('loading');
  protected terrainDetail = signal<string | null>(null);

  protected tick = signal(0);
  protected rtt = signal<number | null>(null);
  protected mapId = signal<string | null>(null);
  protected mapSize = signal<string | null>(null);
  protected keyframe = signal<{ runs: number; tiles: number; bytes: number } | null>(null);

  protected yourSlot = signal(0);
  protected slots = signal<readonly SlotView[]>([]);
  protected standings = signal<readonly StandingView[]>([]);

  /** Własne państwo. `null`, dopóki nie przyjdzie pierwszy `MyState` — czyli przez tik. */
  protected own = signal<OwnState | null>(null);

  /** Krótka odpowiedź na rozkaz, na przykład powód odrzucenia. Gaśnie sama. */
  protected notice = signal<string | null>(null);

  private noticeTimer: ReturnType<typeof setTimeout> | undefined;

  /** Jak długo widać odpowiedź na rozkaz. */
  private static readonly noticeMs = 2500;

  /** Ile ludzi pójdzie w rozkazie przy obecnym ustawieniu suwaka. */
  protected outgoing = computed(() => ((this.own()?.population ?? 0) * this.sendPercent()) / 100);

  /**
   * Zapełnienie puli ludzi w procentach — szerokość paska w dolnym HUD-zie.
   *
   * Obcięte do stu, bo pula potrafi przekroczyć sufit: wracająca z odwrotu armia wchodzi
   * do niej **bez** obcięcia (sufit hamuje werbunek, a nie powroty), a pasek szerszy niż
   * jego tor wylałby się na sąsiednie elementy.
   */
  protected popPercent = computed(() => {
    const state = this.own();

    if (!state || state.maxPopulation <= 0) {
      return 0;
    }

    return Math.min(100, Math.round((state.population / state.maxPopulation) * 100));
  });

  /**
   * Czas meczu liczony z tiku, a nie z zegara przeglądarki.
   *
   * Tik jest jedyną wspólną osią czasu w meczu, więc licznik zbudowany na `Date.now()`
   * rozjeżdżałby się z tym, co widzą pozostali gracze, o cały czas oczekiwania na bilet.
   */
  protected clock = computed(() => {
    const seconds = Math.floor(this.tick() / 10);
    const minutes = Math.floor(seconds / 60);

    return `${String(minutes).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
  });

  /**
   * Zapełnienie cyklu podatkowego w procentach — pasek nad złotem.
   *
   * Serwer przysyła czas **do** poboru, a pasek pokazuje, ile go już upłynęło: rosnący pasek
   * czyta się jako „zbiera się", a malejący jako „kończy się czas", i tylko pierwsze jest
   * prawdą o tej mechanice.
   */
  protected taxPercent = computed(() => {
    const state = this.own();

    if (!state || state.taxPeriodMs <= 0) {
      return 0;
    }

    const left = Math.min(state.taxPeriodMs, Math.max(0, state.taxInMs));

    return Math.round(((state.taxPeriodMs - left) / state.taxPeriodMs) * 100);
  });

  protected taxSeconds = computed(() => Math.ceil((this.own()?.taxInMs ?? 0) / 1000));

  /**
   * Podpowiedź nad komórką złota: skąd się bierze i co dołoży najbliższy pobór.
   *
   * Przyrost na sekundę nie ma własnego miejsca w pasku i mieć nie musi — to liczba, którą
   * sprawdza się raz na kilka minut, a stała pod okiem konkurowałaby o uwagę z pulą ludzi.
   */
  protected goldTip = computed(() => {
    const state = this.own();

    if (!state) {
      return 'Złoto';
    }

    const period = Math.round(state.taxPeriodMs / 1000);

    return (
      `Złoto: +${formatCompact(state.goldIncome)}/s ` +
      `(baza plus ${state.cities} miast). ` +
      `Podatek: +${formatCompact(state.taxAmount)} za ${this.taxSeconds()} s ` +
      `— co ${period} s skarbiec bierze dziesiątą część ludzi zostawionych w domu.`
    );
  });

  protected canBuildCity = computed(() => {
    const state = this.own();

    return !!state && state.gold >= state.nextCityCost;
  });

  /**
   * Czy mecz w ogóle doszedł do skutku w tej karcie.
   *
   * `mapId` ustawia dopiero `MatchInit`, więc `false` znaczy „nie połączyliśmy się ani razu"
   * — a to jest inna sytuacja niż zerwane połączenie w środku rozgrywki i gracz zasługuje
   * na inne zdanie.
   */
  protected joined = computed(() => this.mapId() !== null);

  /**
   * Mecz skończył się dla tej karty i nic już z niego nie będzie.
   *
   * Stan **terminalny**: worker wyczerpał ponowienia albo serwer zamknął gniazdo normalnie.
   * Nakładka rozgrywki musi wtedy zniknąć w całości — suwak, rozkazy i „wczytywanie terenu"
   * nad pustą planszą obiecują interakcję, której nie ma.
   */
  protected over = computed(() => this.link() === 'closed');

  /** Gracz z perspektywy paska stanu: nick i kolor własnego slotu. */
  protected me = computed(() => this.slots().find((slot) => slot.slot === this.yourSlot()) ?? null);

  protected botCount = computed(() => this.slots().filter((slot) => slot.isBot).length);

  constructor() {
    // Start **wiązany z sygnałem kanwy**, a nie odkładany na mikrozadanie: kanwa siedzi
    // pod `@if (matches.match())`, więc w chwili konstrukcji komponentu jeszcze jej nie ma,
    // a bilet bywa dobierany asynchronicznie przez guard. Jednorazowe „spróbuj później"
    // trafiłoby w pustkę i nigdy nie ponowiło — efekt czeka, aż element faktycznie istnieje.
    effect(() => {
      const canvas = this.surface()?.nativeElement;

      if (canvas) {
        this.start(canvas);
      }
    });

    this.destroyRef.onDestroy(() => this.stop());
  }

  /** Liczba ludzi tak, jak widzi ją gracz — patrz `core/format`. */
  protected people = formatPopulation;

  /** Liczby, których nie dotyczy przelicznik populacji: złoto, kafelki. */
  protected compact = formatCompact;

  /** Nazwa slotu do rankingu; boty mają nick z ziarna, więc zawsze coś tu jest. */
  protected nameOf(slot: number): string {
    return this.slots().find((entry) => entry.slot === slot)?.name ?? `#${slot}`;
  }

  protected colorOf(slot: number): string {
    const found = this.slots().find((entry) => entry.slot === slot);

    return found ? `#${found.colorRgb.toString(16).padStart(6, '0')}` : '#888888';
  }

  private start(canvas: HTMLCanvasElement) {
    // `untracked`, bo efekt ma budzić się wyłącznie od pojawienia się kanwy. Bez tego
    // każde odnowienie biletu przeładowywałoby cały widok meczu.
    const match = untracked(() => this.matches.match());

    if (!match || this.worker) {
      return;
    }

    // Brak `OffscreenCanvas` znaczy, że renderowania nie da się wynieść z wątku głównego.
    // Komunikat zamiast cichego czarnego ekranu — gracz ma wiedzieć, że to przeglądarka,
    // a nie serwer.
    if (typeof canvas.transferControlToOffscreen !== 'function') {
      this.supported.set(false);

      return;
    }

    const worker = new Worker(new URL('./net/game-socket.worker', import.meta.url), {
      type: 'module',
    });

    this.worker = worker;

    worker.addEventListener('message', (event: MessageEvent<FromWorker>) =>
      this.onWorkerMessage(event.data),
    );

    const offscreen = canvas.transferControlToOffscreen();

    const start: ToWorker = {
      type: 'start',
      canvas: offscreen,
      url: match.wsUrl,
      ticket: match.ticket,
      viewport: this.measure(canvas),
      ratio: devicePixelRatio || 1,
    };

    worker.postMessage(start, [offscreen]);

    const observer = new ResizeObserver(() =>
      this.post({
        type: 'viewport',
        viewport: this.measure(canvas),
        ratio: devicePixelRatio || 1,
      }),
    );

    observer.observe(canvas);

    // Pętla klatek **musi** siedzieć tutaj: `requestAnimationFrame` nie istnieje w workerze,
    // a napędzanie rysowania czymkolwiek innym rozjeżdża je z odświeżaniem ekranu. W drugą
    // stronę zależności nie ma — pętla sieciowa chodzi w workerze i karta w tle jej nie dławi.
    const frame = () => {
      this.post({ type: 'frame' });

      this.frameHandle = requestAnimationFrame(frame);
    };

    this.frameHandle = requestAnimationFrame(frame);

    this.destroyRef.onDestroy(() => observer.disconnect());
  }

  private measure(canvas: HTMLCanvasElement) {
    // Piksele urządzenia, nie CSS: na ekranie z dpr 2 kafelek narysowany w pikselach CSS
    // byłby rozmyty mimo wyłączonego wygładzania.
    const ratio = devicePixelRatio || 1;

    return {
      width: Math.max(1, Math.round(canvas.clientWidth * ratio)),
      height: Math.max(1, Math.round(canvas.clientHeight * ratio)),
    };
  }

  private post(message: ToWorker) {
    this.worker?.postMessage(message);
  }

  private async onWorkerMessage(message: FromWorker) {
    switch (message.type) {
      case 'link':
        this.link.set(message.state);
        this.linkDetail.set(message.detail ?? null);
        break;

      case 'init':
        this.mapId.set(message.mapId);
        this.mapSize.set(`${message.width}×${message.height}`);
        this.yourSlot.set(message.yourSlot);
        this.slots.set(message.slots);
        break;

      case 'terrain':
        this.terrain.set(message.state);
        this.terrainDetail.set(message.detail ?? null);
        break;

      case 'map':
        this.keyframe.set({ runs: message.runs, tiles: message.tiles, bytes: message.bytes });
        break;

      case 'tick':
        this.tick.set(message.tick);
        break;

      case 'rtt':
        this.rtt.set(message.milliseconds);
        break;

      case 'standings':
        this.standings.set(message.entries);
        break;

      case 'own-state':
        this.own.set(message.state);
        break;

      case 'order-rejected':
        this.notice.set(message.detail);

        // Komunikat gaśnie sam: to informacja zwrotna do kliknięcia, nie stan meczu.
        // Zostawiony na stałe zasłaniałby mapę długo po tym, jak przestał cokolwiek znaczyć.
        clearTimeout(this.noticeTimer);
        this.noticeTimer = setTimeout(() => this.notice.set(null), Match.noticeMs);
        break;

      case 'need-ticket':
        await this.supplyTicket();
        break;

      case 'fatal':
        this.problem.set(message.detail);
        break;
    }
  }

  /**
   * Dobiera świeży bilet dla workera.
   *
   * To jest reconnect z D14 w całości: worker nie ma dostępu ani do HTTP, ani do tożsamości
   * gracza, więc odnowieniem zajmuje się wątek główny — tą samą ścieżką, którą guard
   * wpuszcza gracza po odświeżeniu strony.
   */
  private async supplyTicket() {
    const match = this.matches.match();

    if (!match) {
      return;
    }

    const issued = await this.matches.ensureTicket(match.matchId);
    const renewed = this.matches.match();

    if (!issued || !renewed) {
      this.problem.set('Ten mecz już nie przyjmuje graczy. Wracam do kolejki.');

      await this.router.navigate(['/']);

      return;
    }

    this.post({ type: 'ticket', ticket: renewed.ticket });
  }

  // ─────────────────────────────── wskaźnik ───────────────────────────────

  protected onPointerDown(event: PointerEvent) {
    (event.target as HTMLElement).setPointerCapture(event.pointerId);

    this.dragPointer = event.pointerId;
    this.dragX = event.clientX;
    this.dragY = event.clientY;
    this.dragDistance = 0;
  }

  protected onPointerMove(event: PointerEvent) {
    if (this.dragPointer !== event.pointerId) {
      return;
    }

    const ratio = devicePixelRatio || 1;
    const dx = event.clientX - this.dragX;
    const dy = event.clientY - this.dragY;

    this.dragDistance += Math.abs(dx) + Math.abs(dy);

    this.post({ type: 'pan', dx: dx * ratio, dy: dy * ratio });

    this.dragX = event.clientX;
    this.dragY = event.clientY;
  }

  protected onPointerUp(event: PointerEvent) {
    if (this.dragPointer !== event.pointerId) {
      return;
    }

    this.dragPointer = null;

    // Próg, a nie zero: nikt nie klika bez drgnięcia ręki, a mysz z czułym czujnikiem
    // notuje piksel przy każdym wciśnięciu.
    if (this.dragDistance > Match.dragThresholdPixels) {
      return;
    }

    const bounds = (event.currentTarget as HTMLElement).getBoundingClientRect();
    const ratio = devicePixelRatio || 1;

    this.post({
      type: 'attack-at',
      x: (event.clientX - bounds.left) * ratio,
      y: (event.clientY - bounds.top) * ratio,
      percent: this.sendPercent(),
    });
  }

  protected onPercentChange(event: Event) {
    this.sendPercent.set(Number((event.target as HTMLInputElement).value));
  }

  // ─────────────────────────────── klawiatura ───────────────────────────────

  protected onKeyDown(event: KeyboardEvent) {
    const key = event.key.toLowerCase();

    if (!Match.panKeys.has(key) || event.ctrlKey || event.altKey || event.metaKey) {
      return;
    }

    // Strzałki na suwaku zostają suwakowi — to jego natywne sterowanie i jedyny sposób,
    // żeby ustawić procent co do jedynki. WSAD przesuwa mapę zawsze, bo w tym widoku nie
    // ma ani jednego pola, w którym litery cokolwiek by znaczyły.
    if (key.startsWith('arrow') && this.isFormControl(event.target)) {
      return;
    }

    event.preventDefault();

    // Powtarzanie klawisza przez system jest tu bez znaczenia: kierunek już wysłaliśmy,
    // a ruch liczy worker z czasu klatki.
    if (this.held.has(key)) {
      return;
    }

    this.held.add(key);
    this.sendPanKeys();
  }

  protected onKeyUp(event: KeyboardEvent) {
    if (this.held.delete(event.key.toLowerCase())) {
      this.sendPanKeys();
    }
  }

  /**
   * Puszcza wszystkie klawisze.
   *
   * Bez tego przełączenie okna z wciśniętym klawiszem zostawia kamerę w ruchu na zawsze:
   * `keyup` przychodzi do okna, które akurat ma fokus, a nie do tego, które zaczęło ruch.
   */
  protected releaseKeys() {
    if (this.held.size === 0) {
      return;
    }

    this.held.clear();
    this.sendPanKeys();
  }

  private sendPanKeys() {
    let dx = 0;
    let dy = 0;

    for (const key of this.held) {
      const [x, y] = Match.panKeys.get(key)!;

      dx += x;
      dy += y;
    }

    // Znak, a nie suma: wciśnięte naraz „w lewo" i „w prawo" znoszą się do zera, a nie
    // sumują do podwójnej prędkości.
    this.post({ type: 'pan-keys', dx: Math.sign(dx), dy: Math.sign(dy) });
  }

  private isFormControl(target: EventTarget | null): boolean {
    const tag = (target as HTMLElement | null)?.tagName;

    return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT';
  }

  /**
   * Kadr na całe terytorium wskazanego gracza.
   *
   * Ten sam rozkaz obsługuje przycisk „wyśrodkuj" i kliknięcie w wiersz rankingu — kadr na
   * cudze państwo jest tą samą operacją co kadr na własne, a mecz nie ma mgły wojny, więc
   * nie ma czego przed nikim chować.
   */
  protected focusOn(slot: number) {
    this.post({ type: 'focus', slot });
  }

  protected buildCity() {
    this.post({ type: 'build-city' });
  }

  protected onWheel(event: WheelEvent) {
    event.preventDefault();

    const ratio = devicePixelRatio || 1;
    const bounds = (event.currentTarget as HTMLElement).getBoundingClientRect();

    this.post({
      type: 'zoom',
      // Wykładniczo, a nie liniowo: krok zoomu ma być tej samej wielkości niezależnie od
      // tego, jak blisko jesteśmy — inaczej przy dużym przybliżeniu jedno drgnięcie kółka
      // wyrzuca widok na drugi koniec mapy.
      factor: Math.exp(-event.deltaY * 0.002),
      x: (event.clientX - bounds.left) * ratio,
      y: (event.clientY - bounds.top) * ratio,
    });
  }

  // ─────────────────────────────── wyjście ───────────────────────────────

  private stop() {
    if (this.frameHandle) {
      cancelAnimationFrame(this.frameHandle);
      this.frameHandle = 0;
    }

    this.post({ type: 'stop' });
    this.worker?.terminate();
    this.worker = null;
  }

  /**
   * Opuszcza mecz na dobre.
   *
   * Wyjście jest jawne i nieodwracalne — gracz, który je wybrał, nie wróci już do tego
   * meczu. Alternatywą byłoby zamknięcie karty, czyli decyzja podjęta przez nieuwagę;
   * skoro rozgrywka jest stanem wyłącznym, musi mieć drzwi, a nie tylko okno.
   */
  protected async leave() {
    const match = this.matches.match();

    this.leaving.set(true);

    this.stop();

    if (match) {
      await this.matches.leave(match.matchId);
    }

    await this.router.navigate(['/']);
  }
}
