import { computed, inject, Service, signal } from '@angular/core';
import { HttpClient } from '@angular/common/http';
import { tap } from 'rxjs';

import { HsvColor, Player, PlayerSession } from '../../types/player';
import { readStoredSession, storeSession } from './player-storage';
import { environment } from '../../environments/environment';

@Service()
export class PlayerService {
  private http = inject(HttpClient);
  private baseUrl = environment.apiUrl;

  private session = signal<PlayerSession | null>(readStoredSession());

  /** Profil bez tokenu — do wyświetlania. */
  playerProfile = computed(() => this.session()?.player ?? null);

  /** Token dla interceptora HTTP i dla huba. Jedno źródło tożsamości dla obu transportów. */
  accessToken = computed(() => this.session()?.accessToken ?? null);

  /**
   * Pobiera profil i odnawia token. Wołane przy każdym starcie aplikacji, dzięki czemu
   * aktywny gracz nigdy nie zbliża się do wygaśnięcia i nie potrzeba refresh-tokenów.
   */
  loadSession() {
    return this.http
      .get<PlayerSession>(this.baseUrl + 'players/me')
      .pipe(tap((session) => this.setSession(session)));
  }

  updatePlayerProfile(nickname: string, color: HsvColor) {
    return this.http
      .put<Player>(this.baseUrl + 'players/me', { nickname, color })
      .pipe(tap((player) => this.setPlayer(player)));
  }

  private setSession(session: PlayerSession) {
    storeSession(session);
    this.session.set(session);
  }

  /** Zmiana profilu nie unieważnia tokenu — w środku jest tylko identyfikator gracza. */
  private setPlayer(player: Player) {
    const current = this.session();

    if (current) {
      this.setSession({ ...current, player });
    }
  }
}
