import { PlayerSession } from '../../types/player';

const storageKey = 'player-session';

/** Klucz sprzed wprowadzenia tokenów. Trzymał sam profil, więc nie da się z niego odtworzyć sesji. */
const legacyKey = 'player';

export function readStoredSession(): PlayerSession | null {
  localStorage.removeItem(legacyKey);

  const raw = localStorage.getItem(storageKey);

  if (!raw) {
    return null;
  }

  try {
    const session = JSON.parse(raw) as PlayerSession;

    // Sesja bez tokenu jest bezużyteczna — traktujemy ją jak brak zapisu,
    // zamiast wysyłać puste nagłówki Authorization przy każdym żądaniu.
    return session.accessToken ? session : null;
  } catch {
    localStorage.removeItem(storageKey);
    return null;
  }
}

export function storeSession(session: PlayerSession) {
  localStorage.setItem(storageKey, JSON.stringify(session));
}
