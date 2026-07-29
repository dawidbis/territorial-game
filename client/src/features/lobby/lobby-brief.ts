import { Component, input } from '@angular/core';

import { LobbyHeader } from '../../types/lobby';

/**
 * Wizytówka lobby: informacje po lewej, kwadratowy podgląd mapy po prawej.
 *
 * Wspólna dla strony głównej i widoku lobby — w lobby stanowi górną część panelu, pod
 * którą dochodzi lista graczy, na stronie głównej jest całą jego zawartością.
 */
@Component({
  selector: 'app-lobby-brief',
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
            <svg
              xmlns="http://www.w3.org/2000/svg"
              fill="none"
              viewBox="0 0 24 24"
              stroke-width="1"
              stroke="currentColor"
              class="size-12 text-primary/40"
              aria-hidden="true"
            >
              <path
                stroke-linecap="round"
                stroke-linejoin="round"
                d="m20.893 13.393-1.135-1.135a2.252 2.252 0 0 1-.421-.585l-1.08-2.16a.414.414 0 0 0-.663-.107.827.827 0 0 1-.812.21l-1.273-.363a.89.89 0 0 0-.738 1.595l.587.39c.59.395.674 1.23.172 1.732l-.2.2c-.212.212-.33.498-.33.796v.41c0 .409-.11.809-.32 1.158l-1.315 2.191a2.11 2.11 0 0 1-1.81 1.025 1.055 1.055 0 0 1-1.055-1.055v-1.172c0-.92-.56-1.747-1.414-2.089l-.655-.261a2.25 2.25 0 0 1-1.383-2.46l.007-.042a2.25 2.25 0 0 1 .29-.787l.09-.15a2.25 2.25 0 0 1 2.37-1.048l1.178.236a1.125 1.125 0 0 0 1.302-.795l.208-.73a1.125 1.125 0 0 0-.578-1.315l-.665-.332-.091.091a2.25 2.25 0 0 1-1.591.659h-.18c-.249 0-.487.1-.662.274a.931.931 0 0 1-1.458-1.137l1.411-2.353a2.25 2.25 0 0 0 .286-.76m11.928 9.869A9 9 0 0 0 8.965 3.525m11.928 9.868A9 9 0 1 1 8.965 3.525"
              />
            </svg>
          </div>
        </div>
      </div>
    </div>
  `,
})
export class LobbyBrief {
  header = input<LobbyHeader | null>(null);
}
