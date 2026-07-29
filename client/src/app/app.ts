import { Component, computed, inject } from '@angular/core';
import { RouterOutlet } from '@angular/router';

import { Nav } from '../layout/nav/nav';
import { ActiveRoute } from '../core/services/active-route';
import { LobbyHub } from '../core/services/lobby-hub';

/**
 * Trasy pełnoekranowe. Same ustawiają wysokość i odstęp od przypiętej nawigacji, więc
 * kontener nie może im dokładać ani `mt-24`, ani szerokości — inaczej widok wystaje poza
 * ekran i pojawiają się suwaki.
 */
const fullBleedRoutes = new Set(['/', '/lobby']);

@Component({
  selector: 'app-root',
  imports: [RouterOutlet, Nav],
  templateUrl: './app.html',
  styleUrl: './app.css',
})
export class App {
  private route = inject(ActiveRoute);

  // Hub wstrzyknięty tu, a nie dopiero w widokach lobby: połączenie ma żyć niezależnie od
  // trasy, żeby wejście na profil czy poradnik nie wypisywało gracza z lobby.
  private lobby = inject(LobbyHub);

  protected contained = computed(() => !fullBleedRoutes.has(this.route.path()));
}
