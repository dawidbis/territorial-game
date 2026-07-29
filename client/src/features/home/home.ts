import { Component, computed, inject } from '@angular/core';
import { RouterLink } from '@angular/router';

import { PlayerService } from '../../core/services/player-service';
import { LobbyHub } from '../../core/services/lobby-hub';
import { LobbyBrief } from '../lobby/lobby-brief';
import { hsvToCss } from '../../core/color';
import { formatCountdown } from '../../core/countdown';

@Component({
  selector: 'app-home',
  imports: [RouterLink, LobbyBrief],
  templateUrl: './home.html',
  styleUrl: './home.css',
})
export class Home {
  protected playerService = inject(PlayerService);
  protected lobby = inject(LobbyHub);

  protected playerColor = computed(() => {
    const color = this.playerService.playerProfile()?.color;

    return color ? hsvToCss(color) : 'transparent';
  });

  protected countdown = computed(() => formatCountdown(this.lobby.secondsLeft()));
}
