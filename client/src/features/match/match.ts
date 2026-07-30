import { Component, computed, inject, signal } from '@angular/core';
import { Router } from '@angular/router';

import { MatchGateway } from '../../core/services/match-gateway';
import { ServerClock } from '../../core/services/server-clock';
import { formatCountdown } from '../../core/countdown';

/**
 * Widok meczu — na razie zaślepka.
 *
 * Tu wejdzie kanwa, worker i strumień protobuf. Dopóki nie ma game-serwera, ekran pokazuje
 * to, co gracz faktycznie ma w ręku: adres i bilet. Trasa istnieje już teraz, bo to ona
 * domyka ścieżkę „lobby → mecz" — bez niej `MatchReady` nie miałby dokąd prowadzić.
 */
@Component({
  selector: 'app-match',
  templateUrl: './match.html',
})
export class Match {
  private router = inject(Router);
  private clock = inject(ServerClock);

  protected matches = inject(MatchGateway);

  protected renewing = signal(false);
  protected problem = signal<string | null>(null);

  /** Ile sekund został ważny bilet. Liczone czasem serwera, tak samo jak licznik lobby. */
  protected ticketSecondsLeft = computed(() => {
    const match = this.matches.match();

    if (!match) {
      return null;
    }

    return Math.max(0, Math.ceil((Date.parse(match.expiresAt) - this.clock.now()) / 1000));
  });

  protected ticketCountdown = computed(() => formatCountdown(this.ticketSecondsLeft()));

  protected expired = computed(() => this.ticketSecondsLeft() === 0);

  /** Dobiera świeży bilet. Ta sama ścieżka, którą guard wpuszcza gracza po odświeżeniu strony. */
  protected async renew() {
    const match = this.matches.match();

    if (!match) {
      return;
    }

    this.renewing.set(true);
    this.problem.set(null);

    const issued = await this.matches.ensureTicket(match.matchId);

    this.renewing.set(false);

    if (!issued) {
      // Serwer odmówił, więc bilet przepadł razem z meczem — nie ma na co patrzeć.
      this.problem.set('Ten mecz już nie przyjmuje graczy. Wracam do kolejki.');

      await this.router.navigate(['/']);
    }
  }

  protected async leave() {
    this.matches.release();

    await this.router.navigate(['/']);
  }
}
