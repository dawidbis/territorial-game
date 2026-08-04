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

/** Prefiksy tras pełnoekranowych z parametrem — dokładne dopasowanie by ich nie złapało. */
const fullBleedPrefixes = ['/match/'];

const isFullBleed = (path: string) =>
  fullBleedRoutes.has(path) || fullBleedPrefixes.some((prefix) => path.startsWith(prefix));

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

  protected contained = computed(() => !isFullBleed(this.route.path()));

  /**
   * Mecz zabiera cały ekran: bez nawigacji i bez nakładek kineskopu.
   *
   * Nawigacja znika, bo w trakcie rozgrywki nie ma dokąd wyjść — pozostałe drogi domyka
   * `redirectPlayersToMatch`, a widoczne, lecz nieklikalne menu byłoby obietnicą bez pokrycia.
   *
   * Nakładki znikają z innego powodu: winieta gasi krawędzie do 85% czerni, a linie ramki
   * dokładają 32% co trzeci piksel. Na tekście to klimat, na mapie terenu — utrata
   * czytelności dokładnie tam, gdzie gracz podejmuje decyzje.
   */
  protected inMatch = computed(() => this.route.path().startsWith('/match/'));
}
