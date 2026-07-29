import { computed, DestroyRef, inject, Service, signal } from '@angular/core';
import {
  HubConnection,
  HubConnectionBuilder,
  HubConnectionState,
  LogLevel,
} from '@microsoft/signalr';
import { lastValueFrom } from 'rxjs';

import { environment } from '../../environments/environment';
import { JoinResult, LobbyHeader, LobbyRoster } from '../../types/lobby';
import { PlayerService } from './player-service';
import { ServerClock } from './server-clock';

export type HubStatus = 'connecting' | 'connected' | 'offline';

/** `Offline` to stan klienta, nie odpowiedź serwera — stąd osobny typ. */
export type JoinOutcome = JoinResult | 'Offline';

const reconnectDelayMs = 3000;

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

  private headerState = signal<LobbyHeader | null>(null);
  private rosterState = signal<LobbyRoster | null>(null);
  private statusState = signal<HubStatus>('connecting');

  /** Czy gracz chce siedzieć w lobby. Steruje powrotem po reconnekcie i po zmianie lobby. */
  private membershipWanted = signal(false);

  private connection: HubConnection;
  private connectLoop: Promise<void> | null = null;
  private destroyed = false;

  header = this.headerState.asReadonly();
  status = this.statusState.asReadonly();

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

  /** Czy licznik faktycznie biegnie — póki nie ma game-serwera, stoi. */
  countdownRunning = computed(() => this.headerState()?.startsAt !== null);

  joined = computed(() => {
    const playerId = this.players.playerProfile()?.id;

    return !!playerId && this.roster().some((player) => player.playerId === playerId);
  });

  constructor() {
    this.connection = this.build();

    inject(DestroyRef).onDestroy(() => {
      this.destroyed = true;
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

  private onHeader(header: LobbyHeader) {
    const previousLobbyId = this.headerState()?.lobbyId;

    this.clock.sync(header.serverNow);
    this.headerState.set(header);

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
}
