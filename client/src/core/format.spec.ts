import { describe, expect, it } from 'vitest';

import { formatCompact, formatPopulation } from './format';

describe('formatCompact', () => {
  it('nie skraca liczb poniżej tysiąca', () => {
    expect(formatCompact(0)).toBe('0');
    expect(formatCompact(999)).toBe('999');
  });

  it('skraca tysiące i miliony do jednego miejsca po przecinku', () => {
    expect(formatCompact(1_000)).toBe('1.0k');
    expect(formatCompact(12_142)).toBe('12.1k');
    expect(formatCompact(999_900)).toBe('999.9k');
    expect(formatCompact(1_000_000)).toBe('1.0m');
    expect(formatCompact(2_450_000)).toBe('2.5m');
  });

  it('trzyma zero po przecinku, żeby licznik nie zmieniał szerokości', () => {
    // Przy suficie populacji te dwie wartości pojawiają się naprzemiennie co tik.
    expect(formatCompact(1_000)).toHaveLength(formatCompact(1_400).length);
  });

  it('nie pokazuje wartości ujemnych', () => {
    expect(formatCompact(-5)).toBe('0');
  });
});

describe('formatPopulation', () => {
  it('dzieli przez dziesięć, zanim skróci', () => {
    // 25 000 ludzi w symulacji to 2,5 tysiąca na pasku gracza.
    expect(formatPopulation(25_000)).toBe('2.5k');
    expect(formatPopulation(121_420)).toBe('12.1k');
    expect(formatPopulation(5_000)).toBe('500');

    // Zaokrąglenie wyprzedza próg notacji: 999,9 to już tysiąc, a nie „999".
    expect(formatPopulation(9_999)).toBe('1.0k');
  });
});
