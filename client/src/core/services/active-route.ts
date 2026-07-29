import { computed, inject, Service } from '@angular/core';
import { toSignal } from '@angular/core/rxjs-interop';
import { NavigationEnd, Router } from '@angular/router';
import { filter, map, scan } from 'rxjs';

/**
 * Bieżąca trasa jako sygnał.
 *
 * Aplikacja jest zoneless, więc odczyt `router.url` prosto w szablonie nie ma na czym
 * zawiesić detekcji zmian. Jedno miejsce zamiast powtarzania tej samej subskrypcji
 * w każdym komponencie, który musi wiedzieć, gdzie jesteśmy.
 */
@Service()
export class ActiveRoute {
  private router = inject(Router);

  private navigations = toSignal(
    this.router.events.pipe(
      filter((event) => event instanceof NavigationEnd),
      scan((count) => count + 1, 0),
    ),
    { initialValue: 0 },
  );

  path = toSignal(
    this.router.events.pipe(
      filter((event) => event instanceof NavigationEnd),
      map((event) => event.urlAfterRedirects.split('?')[0]),
    ),
    { initialValue: this.router.url.split('?')[0] },
  );

  /**
   * Czy jest dokąd wracać wewnątrz aplikacji.
   *
   * Druga nawigacja oznacza, że pod bieżącą trasą leży w historii jakaś nasza. Bez tego
   * `location.back()` u kogoś, kto wszedł prosto z zakładki, wyrzuciłby go z serwisu.
   */
  canGoBack = computed(() => this.navigations() > 1);
}
