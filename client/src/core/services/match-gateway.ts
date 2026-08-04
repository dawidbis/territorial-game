import { HttpClient } from '@angular/common/http';
import { inject, Service, signal } from '@angular/core';
import { lastValueFrom } from 'rxjs';

import { environment } from '../../environments/environment';
import { ActiveMatch, MatchReady, MatchTicket } from '../../types/match';
import { ServerClock } from './server-clock';

/** Mecze porzucone w tej karcie. Klucz sesji, nie lokalny — patrz {@link MatchGateway.abandon}. */
const ABANDONED_KEY = 'abandoned-matches';

/**
 * Bilet do meczu przydzielonego temu graczowi.
 *
 * Trzymany **wyłącznie w pamięci**. Sześćdziesięciosekundowe poświadczenie nie ma po co
 * trafiać do `localStorage`: przeżyłoby tam swoją ważność o wiele dni, a pamięć umiera
 * razem z kartą.
 *
 * Utrata biletu nie jest awarią, tylko normalnym stanem po odświeżeniu strony:
 * {@link ensureTicket} dobiera go z serwera, o ile gracz nadal jest uczestnikiem żywego
 * meczu, a {@link resume} odpowiada na pytanie „czy w ogóle w czymś gram" — bo mecz jest
 * stanem wyłącznym i gracz ma do niego wrócić z każdej innej strony.
 */
@Service()
export class MatchGateway {
  private http = inject(HttpClient);
  private clock = inject(ServerClock);

  private assigned = signal<MatchReady | null>(null);

  /** Czy pytaliśmy już serwer w tej sesji. Bez tego guard pytałby przy każdej nawigacji. */
  private asked = false;

  /** Mecz przydzielony temu graczowi albo `null`, gdy żadnego nie ma. */
  match = this.assigned.asReadonly();

  assign(match: MatchReady) {
    this.assigned.set(match);
    this.asked = true;
  }

  /** Porzuca bilet w pamięci. Nie oznacza rezygnacji z meczu — patrz {@link abandon}. */
  release() {
    this.assigned.set(null);
  }

  /**
   * Trwający mecz tego gracza, dobrany z serwera razem z biletem.
   *
   * Pytanie idzie **raz na wejście do aplikacji**: odpowiedź twierdząca kończy się wejściem
   * do gry, a przecząca zmienia się dopiero wtedy, gdy przyjdzie `MatchReady` — a ten
   * ustawia stan sam. Guard woła to przy każdej nawigacji, więc bez tej pamięci pytanie
   * leciałoby przy każdym kliknięciu w menu.
   */
  async resume(): Promise<MatchReady | null> {
    const current = this.assigned();

    if (current) {
      return current;
    }

    if (this.asked) {
      return null;
    }

    this.asked = true;

    try {
      const active = await lastValueFrom(
        this.http.get<ActiveMatch>(`${environment.apiUrl}matches/mine`),
      );

      if (this.isAbandoned(active.matchId)) {
        return null;
      }

      this.assigned.set(active);

      return active;
    } catch {
      // 404 znaczy „nie grasz w niczym" i jest najczęstszą odpowiedzią.
      return null;
    }
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
      this.asked = true;

      return true;
    } catch {
      // 404 znaczy „nie grasz w tym meczu albo on już nie żyje" — dla klienta jest to
      // jedna i ta sama odpowiedź: nie ma dokąd wchodzić.
      this.release();

      return false;
    }
  }

  /**
   * Opuszcza mecz — nieodwracalnie.
   *
   * Decyzja zapada **po stronie serwera**: od tej chwili meta nie wyda już biletu do tego
   * meczu i przestaje go widzieć w odpowiedzi na „w czym gram". Bez tego przycisk byłby
   * wyłącznie schowaniem okna, a gracz wracałby do rozgrywki przy pierwszym odświeżeniu.
   *
   * Slot zostaje zajęty i tak ma być: proces meczu prowadzi aktora dalej, a terytorium nie
   * znika dlatego, że ktoś zamknął kartę.
   */
  async leave(matchId: string): Promise<void> {
    try {
      await lastValueFrom(
        this.http.post(`${environment.apiUrl}matches/${matchId}/leave`, null),
      );
    } catch {
      // Serwer nie przyjął wyjścia — najczęściej dlatego, że meta uważa mecz za żywy,
      // a proces po drugiej stronie już nie żyje. Zapamiętujemy decyzję lokalnie, żeby
      // gracz nie został wciągnięty z powrotem na ekran, z którego właśnie wychodzi.
      this.rememberAbandoned(matchId);
    }

    this.release();
  }

  /**
   * Zapasowa pamięć wyjścia, gdy serwer go nie przyjął.
   *
   * `sessionStorage`, więc przeżywa odświeżenie strony, ale nie zamknięcie karty —
   * dokładnie tyle, ile trwa nieporozumienie między meta a nieżyjącym procesem meczu.
   */
  private rememberAbandoned(matchId: string) {
    const abandoned = this.abandonedIds();

    if (!abandoned.includes(matchId)) {
      sessionStorage.setItem(ABANDONED_KEY, JSON.stringify([...abandoned, matchId]));
    }
  }

  isAbandoned(matchId: string): boolean {
    return this.abandonedIds().includes(matchId);
  }

  private abandonedIds(): string[] {
    try {
      const raw = sessionStorage.getItem(ABANDONED_KEY);
      const parsed: unknown = raw ? JSON.parse(raw) : [];

      return Array.isArray(parsed) ? parsed.filter((id) => typeof id === 'string') : [];
    } catch {
      // Uszkodzony wpis nie ma prawa zablokować wejścia do gry.
      return [];
    }
  }
}
