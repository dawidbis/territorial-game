export type Player = {
  id:         string;
  nickname:   string;
  color:      HsvColor;
}

export type HsvColor = {
  hue:        number;
  saturation: number;
  value:      number;
}

/**
 * Odpowiedź na GET /players/me. Token JEST tożsamością gościa — zastąpił identyfikator
 * trzymany dotąd w localStorage, bo przeglądarka nie ustawi własnego nagłówka na
 * handshake'u WebSocketa, a lobby potrzebuje tej samej tożsamości co REST.
 */
export type PlayerSession = {
  player:      Player;
  accessToken: string;
  expiresAt:   string;
}
