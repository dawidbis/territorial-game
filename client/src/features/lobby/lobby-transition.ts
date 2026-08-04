import { Component, computed, inject } from '@angular/core';

import { Emblem } from '../../layout/emblem';
import { LobbyHub } from '../../core/services/lobby-hub';

/**
 * Zasłona na czas przeskoku licznika.
 *
 * Ta sama forma co ekran startowy aplikacji — czarne tło i godło — bo to ten sam rodzaj
 * czekania i gracz ma go rozpoznać bez czytania.
 *
 * Zasłania **cały panel**, a nie sam licznik: przy starcie meczu zmienia się też nazwa mapy,
 * liczba graczy i roster, więc odsłonięta reszta przeskakiwałaby razem z zegarem.
 *
 * Napis mówi, co się dzieje naprawdę, a te dwie rzeczy są różne: przy starcie meczu serwer
 * jest przydzielany, a przy pustym lobby po prostu leci nowe okno. Jeden wspólny komunikat
 * byłby w połowie przypadków nieprawdą.
 */
@Component({
  selector: 'app-lobby-transition',
  imports: [Emblem],
  template: `
    <div
      role="status"
      class="absolute inset-0 z-10 flex flex-col items-center justify-center gap-5 bg-base-100"
    >
      <app-emblem class="size-20 text-primary/70" />

      <p class="crt-glow text-xs tracking-[0.3em] text-primary uppercase">{{ label() }}</p>

      <span class="sr-only">{{ description() }}</span>
    </div>
  `,
})
export class LobbyTransition {
  private hub = inject(LobbyHub);

  protected label = computed(() =>
    this.hub.allocating() ? 'allocating server...' : 'new window...',
  );

  protected description = computed(() =>
    this.hub.allocating()
      ? 'Trwa uruchamianie meczu. Za chwilę otworzy się kolejne lobby.'
      : 'Nikt nie dołączył, więc okno zbierania startuje od nowa.',
  );
}
