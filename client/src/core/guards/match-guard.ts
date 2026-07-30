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
