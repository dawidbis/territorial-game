import { Component, input } from '@angular/core';

import { LobbyHeader } from '../../types/lobby';
import { Emblem } from '../../layout/emblem';

/**
 * Wizytówka lobby: informacje po lewej, kwadratowy podgląd mapy po prawej.
 *
 * Wspólna dla strony głównej i widoku lobby — w lobby stanowi górną część panelu, pod
 * którą dochodzi lista graczy, na stronie głównej jest całą jego zawartością.
 */
@Component({
  selector: 'app-lobby-brief',
  imports: [Emblem],
  template: `
    <div class="grid gap-px bg-primary/15 sm:grid-cols-2">
      <dl class="flex flex-col justify-center gap-6 bg-base-200 px-5 py-6">
        @if (header(); as lobby) {
          <div class="grid grid-cols-2 gap-x-4 gap-y-6">
            <div class="flex flex-col gap-1">
              <dt class="text-[10px] tracking-[0.25em] uppercase opacity-60">map</dt>
              <dd class="crt-glow truncate text-xl tracking-[0.1em] text-primary uppercase">
                {{ lobby.mapName }}
              </dd>
            </div>

            <div class="flex flex-col gap-1">
              <dt class="text-[10px] tracking-[0.25em] uppercase opacity-60">mode</dt>
              <dd class="text-xl tracking-[0.1em] text-primary uppercase">{{ lobby.mode }}</dd>
            </div>

            <div class="flex flex-col gap-1">
              <dt class="text-[10px] tracking-[0.25em] uppercase opacity-60">players</dt>
              <dd class="crt-glow text-xl tracking-[0.1em] text-primary tabular-nums">
                {{ lobby.playerCount }}/{{ lobby.maxPlayers }}
              </dd>
            </div>

            <div class="flex flex-col gap-1">
              <dt class="text-[10px] tracking-[0.25em] uppercase opacity-60">bots</dt>
              <dd class="text-xl tracking-[0.1em] text-primary tabular-nums">
                {{ lobby.botCount }}
              </dd>
            </div>
          </div>
        } @else {
          <p class="text-[11px] tracking-[0.3em] uppercase opacity-60">linking...</p>
        }
      </dl>

      <div class="flex items-center justify-center bg-base-200 p-4">
        <!-- aspect-square: podgląd zostaje kwadratowy niezależnie od szerokości panelu,
             a max-w trzyma go w rozsądnym rozmiarze zamiast rozpychać pół ekranu. -->
        <div class="crt-grid relative aspect-square w-full max-w-56 border border-primary/20">
          <div class="absolute top-1.5 left-1.5 size-2.5 border-t border-l border-primary/60"></div>
          <div
            class="absolute top-1.5 right-1.5 size-2.5 border-t border-r border-primary/60"
          ></div>
          <div
            class="absolute bottom-1.5 left-1.5 size-2.5 border-b border-l border-primary/60"
          ></div>
          <div
            class="absolute right-1.5 bottom-1.5 size-2.5 border-r border-b border-primary/60"
          ></div>

          <div class="pointer-events-none absolute inset-x-0 top-1/2 border-t border-primary/15"></div>
          <div class="pointer-events-none absolute inset-y-0 left-1/2 border-l border-primary/15"></div>

          <div class="flex h-full items-center justify-center">
            <app-emblem class="size-12 text-primary/40" [strokeWidth]="1" />
          </div>
        </div>
      </div>
    </div>
  `,
})
export class LobbyBrief {
  header = input<LobbyHeader | null>(null);
}
