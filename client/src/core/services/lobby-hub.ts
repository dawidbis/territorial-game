import { computed, DestroyRef, inject, Service, signal } from '@angular/core';
import { Router } from '@angular/router';
import {
  HubConnection,
  HubConnectionBuilder,
  HubConnectionState,
  LogLevel,
} from '@microsoft/signalr';
import { lastValueFrom } from 'rxjs';

import { environment } from '../../environments/environment';
import { JoinResult, LobbyHeader, LobbyRoster } from '../../types/lobby';
import { MatchReady, MatchStartFailed } from '../../types/match';
import { MatchGateway } from './match-gateway';
import { PlayerService } from './player-service';
import { ServerClock } from './server-clock';

export type HubStatus = 'connecting' | 'connected' | 'offline';

/** `Offline` to stan klienta, nie odpowiedź serwera — stąd osobny typ. */
export type JoinOutcome = JoinResult | 'Offline';

const reconnectDelayMs = 3000;

/**
 * Minimalny czas trwania zasłony na przeskoku licznika.
 *
 * Przy zresetowanym oknie przeskok jest **natychmiastowy** — nie ma żadnej fazy, którą można
 * by przeczekać. Przy starcie meczu faza jest, ale z atrapą alokatora trwa tyle, ile zapis do
 * bazy. W obu przypadkach zasłona bez progu tylko mrugnęłaby, czyli byłaby tak samo
 * bezużyteczna jak jej brak.
 */
const minimumHandoverMs = 800;

/**
 * Połączenie z lobby.
 *
 * Samo podłączenie NIE oznacza dołączenia — strona główna łączy się tylko po to, żeby
 * widzieć nagłówek na żywo. Dopiero {@link join} wprowadza gracza do rostera.
 *
 * Serwer wysyła snapshoty, a nie zdarzenia „dołączył/wyszedł", więc ten serwis nie ma
 * żadnego reducera i nie ma się z czym rozjechać — każda wiadomość zastępuje stan
 * w całości.
 */
@Service()
export class LobbyHub {
  private players = inject(PlayerService);
  private clock = inject(ServerClock);
  private matches = inject(MatchGateway);
  private router = inject(Router);

  private headerState = signal<LobbyHeader | null>(null);
  private rosterState = signal<LobbyRoster | null>(null);
  private statusState = signal<HubStatus>('connecting');
  private startProblemState = signal<string | null>(null);

  /** Czy gracz chce siedzieć w lobby. Steruje powrotem po reconnekcie i po zmianie lobby. */
  private membershipWanted = signal(false);

  /** Dopalacz zasłony: trzyma ją jeszcze przez próg po tym, jak faza startu się skończyła. */
  private handoverLinger = signal(false);
  private handoverTimer: ReturnType<typeof setTimeout> | null = null;

  private connection: HubConnection;
  private connectLoop: Promise<void> | null = null;
  private destroyed = false;

  header = this.headerState.asReadonly();
  status = this.statusState.asReadonly();

  /** Powód nieudanego startu poprzedniego lobby. Znika, gdy zaczyna się kolejny start. */
  startProblem = this.startProblemState.asReadonly();

  /** Lista graczy — pusta, dopóki roster nie dotyczy oglądanego lobby. */
  roster = computed(() => {
    const header = this.headerState();
    const roster = this.rosterState();

    return header && roster?.lobbyId === header.lobbyId ? roster.players : [];
  });

  /**
   * Sekundy do startu. Liczone lokalnie z chwili startu i przesunięcia zegara, więc nie
   * wymagają wiadomości co sekundę. Gdy odliczanie jest zatrzymane, zwracana jest wartość,
   * na której licznik stoi.
   */
  secondsLeft = computed(() => {
    const header = this.headerState();

    if (!header) {
      return null;
    }

    if (header.startsAt === null) {
      return header.frozenSeconds;
    }

    return Math.max(0, Math.ceil((Date.parse(header.startsAt) - this.clock.now()) / 1000));
  });

  /** Czy licznik faktycznie biegnie — przy wyłączonym odliczaniu stoi. */
  countdownRunning = computed(() => this.headerState()?.startsAt !== null);

  /**
   * Czy lobby jest w fazie startu, czyli między zamrożeniem rostera a przydzieleniem
   * serwera. Trwa kilka sekund; przez ten czas licznik stoi na zerze.
   */
  allocating = computed(() => this.headerState()?.state === 'Starting');

  /**
   * Czy licznik właśnie przeskakuje w górę i panel należy zasłonić (`LobbyTransition`).
   *
   * **Przeskok ma dwie przyczyny i obie wyglądają tak samo.** Lobby z graczami idzie przez
   * `Starting` i zostaje zastąpione nowym; lobby puste nie ma z czego zrobić meczu, więc
   * serwer po prostu przesuwa termin i okno leci od nowa (§4.8 dokumentacji). Ten drugi
   * przypadek jest tym, który widać na stronie głównej najczęściej — i nie towarzyszy mu
   * żadna zmiana stanu, tylko nowy `startsAt`.
   *
   * Dlatego wyzwalaczem jest sam fakt, że **termin przesunął się w przyszłość**, a nie
   * konkretny stan lobby. Faza startu dokłada się do tego osobno, bo trwa dłużej niż próg.
   */
  handover = computed(() => this.allocating() || this.handoverLinger());

  joined = computed(() => {
    const playerId = this.players.playerProfile()?.id;

    return !!playerId && this.roster().some((player) => player.playerId === playerId);
  });

  constructor() {
    this.connection = this.build();

    inject(DestroyRef).onDestroy(() => {
      this.destroyed = true;

      if (this.handoverTimer !== null) {
        clearTimeout(this.handoverTimer);
      }

      void this.connection.stop();
    });

    void this.connect();
  }

  /**
   * Dołącza do lobby i zapamiętuje, że gracz tego chce — dzięki temu odświeżenie strony
   * i przerwa w sieci wracają do lobby same, bez klikania.
   */
  async join(): Promise<JoinOutcome> {
    this.membershipWanted.set(true);

    // Wejście do kolejki jest rezygnacją z przydzielonego wcześniej meczu — inaczej gracz
    // siedziałby w lobby na następny mecz, trzymając w ręku bilet do trwającego.
    this.matches.release();

    await this.connect();

    const outcome = await this.invokeJoin();

    // Jedno ponowienie, nie pętla: gdyby świeżo wydana tożsamość też okazała się nieznana,
    // mielibyśmy odnawianie sesji w kółko przy każdym wejściu do lobby.
    return outcome === 'UnknownPlayer' ? this.renewIdentityAndJoin() : outcome;
  }

  async leave(): Promise<void> {
    this.membershipWanted.set(false);
    this.rosterState.set(null);

    if (this.connection.state !== HubConnectionState.Connected) {
      return;
    }

    try {
      await this.connection.invoke('Leave');
    } catch {
      // Rozłączenie i tak zwolni miejsce po stronie serwera.
    }
  }

  private async invokeJoin(): Promise<JoinOutcome> {
    try {
      return await this.connection.invoke<JoinResult>('Join');
    } catch {
      return 'Offline';
    }
  }

  /**
   * Odnawia tożsamość i wraca do lobby po odrzuceniu z powodu nieaktualnej sesji.
   *
   * Samo ponowienie `Join` nic by nie dało: tożsamość połączenia ustala się raz, na
   * handshake'u, więc dopóki gniazdo żyje, hub widzi tego samego nieistniejącego gracza.
   * Dlatego po pobraniu nowej sesji połączenie jest zrywane — nowy token trafia na serwer
   * dopiero z kolejnym handshake'iem.
   */
  private async renewIdentityAndJoin(): Promise<JoinOutcome> {
    try {
      await lastValueFrom(this.players.loadSession());
    } catch {
      return 'Offline';
    }

    // `stop()` odpala onclose, który też woła connect(). Pętla jest memoizowana, więc oba
    // wywołania czekają na to samo połączenie, zamiast otwierać dwa.
    await this.connection.stop();
    await this.connect();

    return this.invokeJoin();
  }

  private build(): HubConnection {
    const connection = new HubConnectionBuilder()
      .withUrl(`${environment.hubUrl}lobby`, {
        // Przeglądarka nie ustawi nagłówka Authorization na handshake'u WebSocketa,
        // więc SignalR dokleja token do query stringu. Fabryka czyta sygnał, a nie
        // zapamiętaną wartość, żeby po odnowieniu sesji reconnect użył świeżego tokenu.
        accessTokenFactory: () => this.players.accessToken() ?? '',
      })
      .withAutomaticReconnect()
      .configureLogging(environment.production ? LogLevel.Warning : LogLevel.Information)
      .build();

    connection.on('LobbyHeader', (header: LobbyHeader) => this.onHeader(header));
    connection.on('LobbyRoster', (roster: LobbyRoster) => this.rosterState.set(roster));
    connection.on('MatchReady', (match: MatchReady) => this.onMatchReady(match));

    connection.on('MatchStartFailed', (failure: MatchStartFailed) =>
      this.startProblemState.set(failure.reason),
    );

    connection.onreconnecting(() => this.statusState.set('connecting'));

    connection.onreconnected(() => {
      this.statusState.set('connected');

      // Rozłączenie zwolniło miejsce w rosterze — trzeba je zająć na nowo.
      if (this.membershipWanted()) {
        void this.join();
      }
    });

    // withAutomaticReconnect ma skończoną liczbę prób; potem wracamy do własnej pętli.
    connection.onclose(() => {
      this.statusState.set('offline');
      void this.connect();
    });

    return connection;
  }

  private connect(): Promise<void> {
    this.connectLoop ??= this.runConnectLoop().finally(() => {
      this.connectLoop = null;
    });

    return this.connectLoop;
  }

  private async runConnectLoop(): Promise<void> {
    while (!this.destroyed && this.connection.state === HubConnectionState.Disconnected) {
      this.statusState.set('connecting');

      try {
        await this.connection.start();
        this.statusState.set('connected');

        return;
      } catch {
        this.statusState.set('offline');

        await new Promise((resolve) => setTimeout(resolve, reconnectDelayMs));
      }
    }
  }

  /**
   * Gracz dostał slot w meczu.
   *
   * Kolejność ma znaczenie: `membershipWanted` gasi się PRZED czymkolwiek innym, bo
   * nagłówek nowego lobby może przyjść w tej samej sekundzie, a gracz wpuszczony do meczu
   * nie może wrócić do kolejki na następny — byłby jednocześnie w meczu i w lobby.
   *
   * Nawigacja wychodzi stąd, a nie z widoku lobby, bo hub żyje w `App`: zaproszenie musi
   * zadziałać także wtedy, gdy gracz czeka na profilu albo w poradniku.
   */
  private onMatchReady(match: MatchReady) {
    this.membershipWanted.set(false);
    this.rosterState.set(null);
    this.startProblemState.set(null);

    this.matches.assign(match);

    void this.router.navigate(['/match', match.matchId]);
  }

  private onHeader(header: LobbyHeader) {
    const previous = this.headerState();
    const previousLobbyId = previous?.lobbyId;

    this.clock.sync(header.serverNow);
    this.headerState.set(header);

    // Zaczyna się kolejny start, więc komunikat o poprzednim, nieudanym, przestaje
    // cokolwiek znaczyć. Kasowanie go wcześniej — przy otwarciu nowego lobby — dawałoby
    // mignięcie, bo nowe lobby przychodzi ułamek sekundy po awarii.
    if (header.state === 'Starting') {
      this.startProblemState.set(null);
    }

    // Próg odliczamy od WEJŚCIA w przeskok, nie od jego końca: przy szybkiej alokacji
    // zasłona ma trwać swoje, a przy wolnej nie ma dokładać do niej niczego.
    if (this.countdownJumpedForward(previous, header)) {
      this.beginHandover();
    }

    if (!previousLobbyId || previousLobbyId === header.lobbyId) {
      return;
    }

    // Poprzednie lobby wystartowało. Członkostwo nie przenosi się do nowego,
    // więc gracz, który nadal chce grać, wraca do kolejki.
    this.rosterState.set(null);

    if (this.membershipWanted()) {
      void this.join();
    }
  }

  /**
   * Czy nowy nagłówek przesuwa licznik w górę.
   *
   * Patrzy na termin, a nie na stan lobby, bo przyczyny przeskoku są dwie — start meczu
   * i zresetowane okno pustego lobby — a widać je identycznie. Pierwszy nagłówek po
   * połączeniu nie jest przeskokiem: nie ma się z czego przesunąć.
   */
  private countdownJumpedForward(previous: LobbyHeader | null, next: LobbyHeader): boolean {
    if (previous?.state !== 'Starting' && next.state === 'Starting') {
      return true;
    }

    if (!previous?.startsAt || !next.startsAt) {
      return false;
    }

    return Date.parse(next.startsAt) > Date.parse(previous.startsAt);
  }

  private beginHandover() {
    if (this.handoverTimer !== null) {
      clearTimeout(this.handoverTimer);
    }

    this.handoverLinger.set(true);

    this.handoverTimer = setTimeout(() => {
      this.handoverTimer = null;
      this.handoverLinger.set(false);
    }, minimumHandoverMs);
  }
}
