import { Routes } from '@angular/router';

import { redirectMembersToLobby } from '../core/guards/lobby-guard';
import { redirectPlayersToMatch, requireMatchTicket } from '../core/guards/match-guard';

// loadComponent, a nie component: każdy widok jest osobnym chunkiem, więc wejście na
// stronę główną nie ściąga profilu, poradnika ani lobby.
//
// `redirectPlayersToMatch` stoi na KAŻDEJ trasie poza samym meczem: rozgrywka jest stanem
// wyłącznym, więc dopóki trwa, nie ma dokąd wyjść. Guard idzie przed pozostałymi, bo mecz
// wygrywa z każdą inną regułą — także z odesłaniem do lobby.
export const routes: Routes = [
  {
    path: '',
    canActivate: [redirectPlayersToMatch, redirectMembersToLobby],
    loadComponent: () => import('../features/home/home').then((m) => m.Home),
  },
  {
    path: 'lobby',
    canActivate: [redirectPlayersToMatch],
    loadComponent: () => import('../features/lobby/lobby').then((m) => m.Lobby),
  },
  {
    path: 'match/:matchId',
    canActivate: [requireMatchTicket],
    loadComponent: () => import('../features/match/match').then((m) => m.Match),
  },
  {
    path: 'guide',
    canActivate: [redirectPlayersToMatch],
    loadComponent: () => import('../features/guide/guide').then((m) => m.Guide),
  },
  {
    path: 'profile',
    canActivate: [redirectPlayersToMatch],
    loadComponent: () => import('../features/profile/profile').then((m) => m.Profile),
  },
  {
    path: 'contact',
    canActivate: [redirectPlayersToMatch],
    loadComponent: () => import('../features/contact/contact').then((m) => m.Contact),
  },
];
