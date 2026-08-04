import { describe, expect, it } from 'vitest';

import { WATER_OWNER } from './palette';
import { measureTerritories } from './territories';

/**
 * Testy pomiaru terytoriów.
 *
 * Sprawdzane jest to, co widać na ekranie: że podpis państwa stoi **na tym państwie**,
 * a prostokąt obejmujący naprawdę obejmuje. Kotwica postawiona na cudzej ziemi nie wygląda
 * na błąd obliczeń — wygląda na to, że sąsiad ma dwa nicki.
 */
describe('measureTerritories', () => {
  /** Buduje tablicę właścicieli z rysunku: kropka to pustkowie, `~` woda, cyfra to slot. */
  const draw = (rows: readonly string[]) => {
    const width = rows[0].length;
    const owner = new Uint8Array(width * rows.length);

    rows.forEach((row, y) => {
      for (let x = 0; x < width; x++) {
        const cell = row[x];

        owner[y * width + x] =
          cell === '.' ? 0 : cell === '~' ? WATER_OWNER : Number.parseInt(cell, 10);
      }
    });

    return { owner, width, height: rows.length };
  };

  it('liczy kafelki i prostokąt obejmujący każdego slotu osobno', () => {
    const { owner, width, height } = draw([
      '.....',
      '.11..',
      '.11.2',
      '.....',
    ]);

    const measured = measureTerritories(owner, width, height);

    expect(measured[1]).toMatchObject({ tiles: 4, minX: 1, minY: 1, maxX: 2, maxY: 2 });
    expect(measured[2]).toMatchObject({ tiles: 1, minX: 4, minY: 2, maxX: 4, maxY: 2 });

    // Pustkowie i woda nie są państwami i nie mają się podpisywać.
    expect(measured[0]).toBeNull();
    expect(measured[WATER_OWNER]).toBeNull();
    expect(measured[3]).toBeNull();
  });

  it('stawia kotwicę w środku ciężkości, gdy ten leży na własnej ziemi', () => {
    const { owner, width, height } = draw([
      '11111',
      '11111',
      '11111',
    ]);

    expect(measureTerritories(owner, width, height)[1]).toMatchObject({
      anchorX: 2,
      anchorY: 1,
    });
  });

  /**
   * Państwo w kształcie podkowy ma środek ciężkości w dziurze pośrodku. Podpis postawiony
   * tam ląduje na cudzej ziemi albo na pustkowiu — dlatego wtedy wygrywa środek
   * najdłuższego własnego ciągu, który z definicji leży na terytorium.
   */
  it('ucieka ze środka ciężkości, gdy ten wypada poza terytorium', () => {
    const { owner, width, height } = draw([
      '11111',
      '1...1',
      '1...1',
      '1...1',
    ]);

    const anchor = measureTerritories(owner, width, height)[1]!;

    expect(owner[anchor.anchorY * width + anchor.anchorX]).toBe(1);
    expect(anchor.anchorY).toBe(0);
  });

  it('nie gubi się na pustej mapie', () => {
    const { owner, width, height } = draw(['...', '~~~']);

    expect(measureTerritories(owner, width, height).every((entry) => entry === null)).toBe(true);
  });
});
