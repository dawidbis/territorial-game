import { HttpInterceptorFn } from '@angular/common/http';
import { inject } from '@angular/core';

import { PlayerService } from '../services/player-service';
import { environment } from '../../environments/environment';

/**
 * Dokleja token do żądań do własnego API.
 *
 * Token czytany jest z sygnału, a nie z localStorage — poprzednia wersja parsowała JSON
 * przy każdym żądaniu. Nagłówek `Authorization` zastąpił `X-Player-Id`, który dało się
 * podać ręcznie i przejąć dowolny profil.
 */
export const authInterceptor: HttpInterceptorFn = (req, next) => {
  if (!req.url.startsWith(environment.apiUrl)) {
    return next(req);
  }

  const token = inject(PlayerService).accessToken();

  if (!token) {
    return next(req);
  }

  return next(req.clone({ setHeaders: { Authorization: `Bearer ${token}` } }));
};
