/**
 * Zaproszenie do meczu. Przychodzi wyłącznie do jednego gracza, bo bilet jest
 * poświadczeniem na jego slot — nie ma prawa pojawić się w żadnym broadcaście.
 */
export type MatchReady = {
  matchId: string;
  /** Adres game-serwera. Zawsze przez wspólne wejście, nigdy host:port procesu. */
  wsUrl: string;
  /** Nieprzezroczysty ciąg. Klient go nie interpretuje — od tego jest game-serwer. */
  ticket: string;
  /** Chwila wygaśnięcia biletu w ISO. Po niej trzeba poprosić o nowy. */
  expiresAt: string;
};

/**
 * Odpowiedź na `POST /api/matches/{matchId}/ticket`. To samo co w `MatchReady` minus
 * identyfikator meczu — ten klient ma już w ręku, skoro o niego pytał.
 */
export type MatchTicket = Omit<MatchReady, 'matchId'>;

/**
 * Odpowiedź na `GET /api/matches/mine` — trwający mecz wołającego albo 404.
 *
 * Kształtem to samo co `MatchReady`, ale przychodzi z pytania, a nie z zaproszenia:
 * mecz jest stanem wyłącznym, więc klient upewnia się przy każdym wejściu, czy przypadkiem
 * nie ma dokąd wrócić.
 */
export type ActiveMatch = MatchReady;

/** Start się nie powiódł — mecz nie powstanie, a lobby otworzy się na nowo. */
export type MatchStartFailed = {
  reason: string;
};
