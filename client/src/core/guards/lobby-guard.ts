import { inject } from '@angular/core';
import { CanActivateFn, Router } from '@angular/router';

import { LobbyHub } from '../services/lobby-hub';

/**
 * Odsyła do lobby gracza, który w nim siedzi, a próbuje wejść na stronę główną.
 *
 * Strona główna pokazuje to samo lobby, tylko bez listy graczy — dla kogoś, kto już
 * dołączył, jest krokiem wstecz. Zamiast wygaszać link w nawigacji (martwy link czyta się
 * jak błąd), pozycja „start" zmienia się na „lobby" i prowadzi tam, gdzie gracz jest;
 * ten guard domyka pozostałe drogi — wpisany adres, przycisk wstecz, stary zakładkowany link.
 */
export const redirectMembersToLobby: CanActivateFn = () => {
  const hub = inject(LobbyHub);
  const router = inject(Router);

  return hub.joined() ? router.createUrlTree(['/lobby']) : true;
};
