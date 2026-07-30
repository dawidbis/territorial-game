import { Routes } from '@angular/router';

import { redirectMembersToLobby } from '../core/guards/lobby-guard';
import { requireMatchTicket } from '../core/guards/match-guard';

// loadComponent, a nie component: każdy widok jest osobnym chunkiem, więc wejście na
// stronę główną nie ściąga profilu, poradnika ani lobby.
export const routes: Routes = [
  {
    path: '',
    canActivate: [redirectMembersToLobby],
    loadComponent: () => import('../features/home/home').then((m) => m.Home),
  },
  {
    path: 'lobby',
    loadComponent: () => import('../features/lobby/lobby').then((m) => m.Lobby),
  },
  {
    path: 'match/:matchId',
    canActivate: [requireMatchTicket],
    loadComponent: () => import('../features/match/match').then((m) => m.Match),
  },
  {
    path: 'guide',
    loadComponent: () => import('../features/guide/guide').then((m) => m.Guide),
  },
  {
    path: 'profile',
    loadComponent: () => import('../features/profile/profile').then((m) => m.Profile),
  },
  {
    path: 'contact',
    loadComponent: () => import('../features/contact/contact').then((m) => m.Contact),
  },
];
