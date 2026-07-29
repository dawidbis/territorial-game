import { Component, computed, inject } from '@angular/core';
import { RouterLink, RouterLinkActive } from '@angular/router';

import { LobbyHub } from '../../core/services/lobby-hub';
import { ActiveRoute } from '../../core/services/active-route';
import { formatCountdown } from '../../core/countdown';

@Component({
  selector: 'app-nav',
  imports: [RouterLink, RouterLinkActive],
  templateUrl: './nav.html',
  styleUrl: './nav.css',
})
export class Nav {
  protected lobby = inject(LobbyHub);

  private route = inject(ActiveRoute);

  protected countdown = computed(() => formatCountdown(this.lobby.secondsLeft()));

  /**
   * Plakietka przypomina o lobby na podstronach. Na samym lobby jest zbędna — te same
   * liczby są tam na ekranie — a strona główna jest dla członka lobby nieosiągalna.
   */
  protected showBadge = computed(() => this.lobby.joined() && this.route.path() !== '/lobby');

  protected badgeLabel = computed(() => {
    const header = this.lobby.header();

    if (!header) {
      return 'Jesteś w lobby';
    }

    return `Jesteś w lobby: ${header.playerCount} z ${header.maxPlayers} graczy, do startu ${this.countdown()}`;
  });
}
