import { HttpClient } from '@angular/common/http';
import { inject, Service, signal } from '@angular/core';
import { lastValueFrom } from 'rxjs';

import { environment } from '../../environments/environment';
import { MatchReady, MatchTicket } from '../../types/match';
import { ServerClock } from './server-clock';

/**
 * Bilet do meczu przydzielonego temu graczowi.
 *
 * Trzymany **wyłącznie w pamięci**. Sześćdziesięciosekundowe poświadczenie nie ma po co
 * trafiać do `localStorage`: przeżyłoby tam swoją ważność o wiele dni, a pamięć umiera
 * razem z kartą — co akurat tutaj jest zaletą, bo zamknięcie karty to najczęstszy sposób,
 * w jaki gracz rezygnuje z meczu.
 *
 * Utrata biletu nie jest więc awarią, tylko normalnym stanem po odświeżeniu strony:
 * {@link ensureTicket} dobiera go z serwera, o ile gracz nadal jest uczestnikiem żywego
 * meczu. Ta sama ścieżka obsługuje przegapione `MatchReady` i powrót po zerwaniu połączenia.
 */
@Service()
export class MatchGateway {
  private http = inject(HttpClient);
  private clock = inject(ServerClock);

  private assigned = signal<MatchReady | null>(null);

  /** Mecz przydzielony temu graczowi albo `null`, gdy żadnego nie ma. */
  match = this.assigned.asReadonly();

  assign(match: MatchReady) {
    this.assigned.set(match);
  }

  /** Porzuca bilet — gracz rezygnuje z meczu i wraca do kolejki. */
  release() {
    this.assigned.set(null);
  }

  /**
   * Upewnia się, że mamy ważny bilet do wskazanego meczu.
   *
   * Ważność liczona jest czasem serwera, nie systemowym: zegar gracza bywa przesunięty
   * o minuty, a wtedy bilet byłby albo odnawiany bez potrzeby przy każdym wejściu, albo —
   * gorzej — uznawany za ważny długo po wygaśnięciu.
   *
   * @returns `false`, gdy serwer odmówił — gracz nie jest uczestnikiem albo mecz już nie żyje.
   */
  async ensureTicket(matchId: string): Promise<boolean> {
    const current = this.assigned();

    if (current?.matchId === matchId && Date.parse(current.expiresAt) > this.clock.now()) {
      return true;
    }

    try {
      const ticket = await lastValueFrom(
        this.http.post<MatchTicket>(`${environment.apiUrl}matches/${matchId}/ticket`, null),
      );

      this.assigned.set({ matchId, ...ticket });

      return true;
    } catch {
      // 404 znaczy „nie grasz w tym meczu albo on już nie żyje" — dla klienta jest to
      // jedna i ta sama odpowiedź: nie ma dokąd wchodzić.
      this.release();

      return false;
    }
  }
}
