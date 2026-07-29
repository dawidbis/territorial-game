import { Component, computed, inject, signal } from '@angular/core';
import { Router } from '@angular/router';

import { LobbyHub, JoinOutcome } from '../../core/services/lobby-hub';
import { PlayerService } from '../../core/services/player-service';
import { LobbyBrief } from './lobby-brief';
import { hsvToCss } from '../../core/color';
import { formatCountdown } from '../../core/countdown';

@Component({
  selector: 'app-lobby',
  imports: [LobbyBrief],
  templateUrl: './lobby.html',
})
export class Lobby {
  private router = inject(Router);

  protected hub = inject(LobbyHub);
  protected playerService = inject(PlayerService);

  protected outcome = signal<JoinOutcome | null>(null);
  protected leaving = signal(false);

  protected countdown = computed(() => formatCountdown(this.hub.secondsLeft()));

  /**
   * Kolor liczony raz na zmianę rostera, a nie przy każdym cyklu detekcji zmian —
   * zegar odświeża się cztery razy na sekundę, a graczy może być stu.
   */
  protected players = computed(() =>
    this.hub.roster().map((player) => ({ ...player, css: hsvToCss(player.color) })),
  );

  protected myId = computed(() => this.playerService.playerProfile()?.id ?? null);

  /** Komunikat pokazywany, gdy dołączenie się nie udało. `null` oznacza brak problemu. */
  protected problem = computed(() => {
    switch (this.outcome()) {
      case 'Full':
        return 'Lobby jest pełne. Poczekaj na następne — otworzy się zaraz po starcie tego.';
      case 'NotGathering':
        return 'To lobby weszło już w fazę startu. Za chwilę otworzy się następne.';
      // Widoczne dopiero, gdy odnowiona sesja też została odrzucona — samo wygaśnięcie
      // tożsamości serwis naprawia po cichu i wynik jest wtedy inny.
      case 'UnknownPlayer':
        return 'Nie udało się potwierdzić tożsamości gracza. Odśwież stronę.';
      case 'Offline':
        return 'Brak połączenia z serwerem. Próba ponowienia trwa.';
      default:
        return null;
    }
  });

  constructor() {
    // Dołączenie jest idempotentne po stronie serwera, więc wejście na tę trasę —
    // także przez odświeżenie strony albo wklejenie adresu — po prostu wraca do lobby.
    void this.join();
  }

  protected async join() {
    this.outcome.set(await this.hub.join());
  }

  protected async leave() {
    this.leaving.set(true);

    await this.hub.leave();
    await this.router.navigate(['/']);
  }
}
