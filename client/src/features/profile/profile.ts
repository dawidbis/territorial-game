import { Component, computed, inject, signal } from '@angular/core';
import { Location } from '@angular/common';
import { Router } from '@angular/router';
import { PlayerService } from '../../core/services/player-service';
import { ActiveRoute } from '../../core/services/active-route';
import { hsvToCss } from '../../core/color';

/** Odpowiednik Nickname.TryCreate z serwera: 3-20 znaków, litery, cyfry, '-' i '_'. */
const nicknamePattern = /^[\p{L}\p{N}_-]{3,20}$/u;

@Component({
  selector: 'app-profile',
  templateUrl: './profile.html',
  styleUrl: './profile.css',
})
export class Profile {
  protected playerService = inject(PlayerService);

  private location = inject(Location);
  private router = inject(Router);
  private route = inject(ActiveRoute);

  protected nickname = signal('');
  protected hue = signal(0);
  protected saturation = signal(0);
  protected value = signal(0);

  protected saving = signal(false);
  protected error = signal<string | null>(null);

  /**
   * Końcówka identyfikatora, nie początek: pierwsze 12 znaków UUIDv7 to znacznik
   * czasu, wspólny dla wszystkich graczy z danego okresu. Te same 6 znaków trafia
   * do nicku gościa, więc po zmianie nicku gracz nadal widzi swój pierwotny tag.
   */
  protected shortId = computed(() =>
    (this.playerService.playerProfile()?.id ?? '').slice(-6).toUpperCase(),
  );

  protected preview = computed(() =>
    hsvToCss({ hue: this.hue(), saturation: this.saturation(), value: this.value() }),
  );

  /** Suwak nasycenia w bieżącym odcieniu — od szarości do pełnego koloru. */
  protected saturationTrack = computed(
    () =>
      `linear-gradient(to right, ${hsvToCss({ hue: this.hue(), saturation: 0, value: this.value() })}, ${hsvToCss({ hue: this.hue(), saturation: 100, value: this.value() })})`,
  );

  /** Suwak jasności — od czerni do pełnej jasności bieżącego koloru. */
  protected valueTrack = computed(
    () =>
      `linear-gradient(to right, #000, ${hsvToCss({ hue: this.hue(), saturation: this.saturation(), value: 100 })})`,
  );

  protected nicknameValid = computed(() => nicknamePattern.test(this.nickname().trim()));

  protected dirty = computed(() => {
    const profile = this.playerService.playerProfile();

    if (!profile) {
      return false;
    }

    return (
      this.nickname().trim() !== profile.nickname ||
      this.hue() !== profile.color.hue ||
      this.saturation() !== profile.color.saturation ||
      this.value() !== profile.color.value
    );
  });

  constructor() {
    // Profil jest pobrany w provideAppInitializer, więc w tym miejscu już go mamy.
    this.reset();
  }

  protected reset() {
    const profile = this.playerService.playerProfile();

    this.error.set(null);
    this.nickname.set(profile?.nickname ?? '');
    this.hue.set(profile?.color.hue ?? 0);
    this.saturation.set(profile?.color.saturation ?? 0);
    this.value.set(profile?.color.value ?? 0);
  }

  protected save() {
    if (!this.nicknameValid() || this.saving()) {
      return;
    }

    this.saving.set(true);
    this.error.set(null);

    this.playerService
      .updatePlayerProfile(this.nickname().trim(), {
        hue: this.hue(),
        saturation: this.saturation(),
        value: this.value(),
      })
      .subscribe({
        next: () => this.saving.set(false),
        error: (cause) => {
          this.saving.set(false);
          this.error.set('Zapis odrzucony przez serwer.');
          console.error('Nie udało się zapisać profilu.', cause);
        },
      });
  }

  /**
   * Wraca tam, skąd gracz przyszedł — najczęściej do lobby, bo profil odwiedza się
   * właśnie z niego. Sztywny link na stronę główną wyrzucałby go z lobby, a przy
   * wejściu prosto z zakładki `location.back()` wyprowadziłoby poza serwis, stąd zapas.
   */
  protected goBack() {
    if (this.route.canGoBack()) {
      this.location.back();
      return;
    }

    void this.router.navigate(['/']);
  }

  protected onNickname(event: Event) {
    this.nickname.set((event.target as HTMLInputElement).value);
  }

  protected onRange(target: 'hue' | 'saturation' | 'value', event: Event) {
    this[target].set(Number((event.target as HTMLInputElement).value));
  }
}
