import { computed, DestroyRef, inject, Service, signal } from '@angular/core';

/** Jak często odświeżany jest czas. Ćwierć sekundy wystarcza, żeby licznik sekund nie zacinał. */
const tickMs = 250;

/**
 * Czas serwera odtworzony po stronie klienta.
 *
 * Serwer wysyła moment startu, a nie „pozostało N sekund" — inaczej licznik wymagałby
 * wiadomości co sekundę i byłby przestarzały o RTT. Żeby odjęcie miało sens mimo źle
 * ustawionego zegara w systemie gracza, każdy snapshot niesie `serverNow`, z którego
 * liczone jest stałe przesunięcie.
 *
 * Jeden interwał na całą aplikację — nie po jednym na komponent pokazujący odliczanie.
 */
@Service()
export class ServerClock {
  private localNow = signal(Date.now());
  private offsetMs = signal(0);

  /** Bieżący czas serwera w milisekundach epoki. */
  now = computed(() => this.localNow() + this.offsetMs());

  constructor() {
    const handle = setInterval(() => this.localNow.set(Date.now()), tickMs);

    inject(DestroyRef).onDestroy(() => clearInterval(handle));
  }

  /** Ustawia przesunięcie na podstawie czasu serwera z ostatniego snapshotu. */
  sync(serverNow: string) {
    const parsed = Date.parse(serverNow);

    if (!Number.isNaN(parsed)) {
      this.offsetMs.set(parsed - Date.now());
    }
  }
}
