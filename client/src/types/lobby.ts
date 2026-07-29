import { HsvColor } from './player';

export type LobbyState = 'Gathering' | 'Starting' | 'Closed';

export type JoinResult =
  | 'Joined'
  | 'AlreadyJoined'
  | 'Full'
  | 'NotGathering'
  /** Token nie wskazuje istniejącego gracza. Naprawialne odnowieniem sesji. */
  | 'UnknownPlayer';

/** To, co widzi każdy — także ktoś, kto tylko otworzył stronę główną. */
export type LobbyHeader = {
  lobbyId: string;
  state: LobbyState;
  mapId: string;
  mapName: string;
  mode: string;
  playerCount: number;
  maxPlayers: number;
  botCount: number;
  /** Chwila startu w ISO. `null`, gdy odliczanie jest zatrzymane. */
  startsAt: string | null;
  /** Wartość, na której stoi zatrzymany licznik. Niepusta dokładnie wtedy, gdy `startsAt` jest `null`. */
  frozenSeconds: number | null;
  /** Czas serwera w chwili zbudowania snapshotu — z niego liczone jest przesunięcie zegara. */
  serverNow: string;
};

export type LobbyPlayer = {
  playerId: string;
  nickname: string;
  color: HsvColor;
};

/** Trafia wyłącznie do graczy, którzy dołączyli. */
export type LobbyRoster = {
  lobbyId: string;
  players: LobbyPlayer[];
};
