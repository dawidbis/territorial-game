import {
  ApplicationConfig,
  inject,
  provideAppInitializer,
  provideBrowserGlobalErrorListeners,
} from '@angular/core';
import {
  PreloadAllModules,
  provideRouter,
  withPreloading,
  withViewTransitions,
} from '@angular/router';
import { provideHttpClient, withInterceptors } from '@angular/common/http';
import { lastValueFrom } from 'rxjs';

import { routes } from './app.routes';
import { authInterceptor } from '../core/interceptors/auth-interceptor';
import { PlayerService } from '../core/services/player-service';

export const appConfig: ApplicationConfig = {
  providers: [
    provideBrowserGlobalErrorListeners(),
    provideRouter(
      routes,
      // skipInitialTransition: pierwsza nawigacja nie ma z czego animować, a próba
      // przejścia w trakcie startu kończy się InvalidStateError w konsoli.
      withViewTransitions({ skipInitialTransition: true }),
      // Animacja przejścia startuje przed doczytaniem chunku trasy, więc czekanie na
      // import() dzieje się W TRAKCIE działającej animacji — a to jest jedyny moment,
      // w którym udało się powtarzalnie wywołać "Transition was aborted".
      // Wstępne ładowanie w czasie bezczynności zdejmuje oczekiwanie ze ścieżki
      // nawigacji: initial bundle zostaje mały, a przejścia nie mają na co czekać.
      withPreloading(PreloadAllModules),
    ),
    provideHttpClient(withInterceptors([authInterceptor])),
    provideAppInitializer(async () => {
      const playerService = inject(PlayerService);

      // Sesja musi być gotowa przed pierwszym renderem: token z niej trafia zarówno do
      // nagłówka Authorization, jak i do handshake'u huba lobby.
      try {
        await lastValueFrom(playerService.loadSession());
      } catch (error) {
        console.error('Nie udało się pobrać profilu gracza.', error);
      } finally {
        document.getElementById('initial-splash')?.remove();
      }
    }),
  ],
};
