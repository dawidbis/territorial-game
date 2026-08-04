import { inject } from '@angular/core';
import { CanActivateFn, Router } from '@angular/router';

import { MatchGateway } from '../services/match-gateway';

/**
 * Wpuszcza na widok meczu wyłącznie z ważnym biletem.
 *
 * Bilet żyje w pamięci, więc po odświeżeniu strony guard próbuje go dobrać z serwera —
 * i to jest cała obsługa powrotu do trwającego meczu. Odmowa serwera znaczy, że gracz nie
 * jest uczestnikiem albo mecz już nie żyje; w obu przypadkach nie ma sensu pokazywać mu
 * ekranu meczu, więc ląduje na stronie głównej.
 */
export const requireMatchTicket: CanActivateFn = async (route) => {
  const gateway = inject(MatchGateway);
  const router = inject(Router);

  const matchId = route.paramMap.get('matchId');

  if (!matchId) {
    return router.createUrlTree(['/']);
  }

  return (await gateway.ensureTicket(matchId)) || router.createUrlTree(['/']);
};

/**
 * Odsyła do meczu gracza, który w nim gra, a próbuje wejść gdziekolwiek indziej.
 *
 * **Mecz jest stanem wyłącznym.** W trakcie rozgrywki nie ma dokąd wyjść: slot jest zajęty,
 * terytorium istnieje dalej, a lobby i profil pokazywałyby stan, którego gracz i tak nie może
 * zmienić. Nawigacja znika z ekranu meczu (patrz `App`), a ten guard domyka pozostałe drogi —
 * wpisany adres, przycisk wstecz, stary zakładkowany link.
 *
 * Pytanie do serwera pada **raz na wejście do aplikacji** i tylko wtedy, gdy pamięć jest
 * pusta — dzięki temu blokada przeżywa odświeżenie strony, nie kosztując żądania przy każdym
 * kliknięciu w menu.
 */
export const redirectPlayersToMatch: CanActivateFn = async () => {
  const gateway = inject(MatchGateway);
  const router = inject(Router);

  const active = await gateway.resume();

  return active ? router.createUrlTree(['/match', active.matchId]) : true;
};
