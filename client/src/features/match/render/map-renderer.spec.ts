import { buildPalette, WATER_OWNER } from './palette';
import { isBorderTile, lighten } from './map-renderer';

/**
 * Rozjaśnianie piksela przy błysku przejęcia.
 *
 * Sam `MapRenderer` wymaga `OffscreenCanvas`, więc testowany jest tu wyłącznie ten fragment,
 * który da się pomylić po cichu: mieszanie kanałów. Pomyłka w kolejności bajtów albo
 * w kanale alfa daje obraz, który *wygląda* na działający — kafelki po prostu mrugają nie
 * tym kolorem albo znikają pod przezroczystością.
 */
describe('lighten', () => {
  const anyColor = 0x11223344;

  it('przy zerowej sile nie rusza koloru', () => {
    expect(lighten(anyColor, 0)).toBe(anyColor);
  });

  it('przy pełnej sile daje biel na wszystkich kanałach', () => {
    expect(lighten(anyColor, 1)).toBe(0xffffffff);
  });

  it('zostawia kanał alfa nietknięty, bo już jest pełny', () => {
    // Tak wygląda każdy wpis palety: alfa 255 w tym bajcie, który akurat wypadł na tej
    // maszynie. Niezależnie od kolejności bajtów mieszanie 255 z bielą musi dać 255 —
    // i to jest powód, dla którego `lighten` nie rozpakowuje kanałów po nazwie.
    const palette = buildPalette([{ slot: 3, colorRgb: 0x3366cc }]);
    const opaque = palette[3 * 4 + 1];

    for (const strength of [0, 0.25, 0.5, 0.85, 1]) {
      const mixed = lighten(opaque, strength);

      // Bajt alfy to ten, który w oryginale ma 255 — szukamy go zamiast zakładać pozycję.
      for (let shift = 0; shift < 32; shift += 8) {
        if (((opaque >>> shift) & 0xff) === 0xff) {
          expect((mixed >>> shift) & 0xff).toBe(0xff);
        }
      }
    }
  });

  it('rozjaśnia monotonicznie razem z siłą', () => {
    const dark = 0x10203040;

    const steps = [0, 0.2, 0.4, 0.6, 0.8, 1].map((strength) => lighten(dark, strength) >>> 0);

    for (let index = 1; index < steps.length; index++) {
      // Każdy kanał osobno, bo porównanie całych słów mieszałoby kanały ze sobą.
      for (let shift = 0; shift < 32; shift += 8) {
        expect((steps[index] >>> shift) & 0xff).toBeGreaterThanOrEqual(
          (steps[index - 1] >>> shift) & 0xff,
        );
      }
    }
  });
});

/**
 * Obrys terytorium.
 *
 * Cała trudność siedzi w warunkach brzegowych: kafelek przy krawędzi mapy nie ma czterech
 * sąsiadów, a odczyt „sąsiada" spoza wiersza trafia w koniec wiersza poprzedniego. Błąd
 * o jeden daje tu obrys wypisany wzdłuż lewej krawędzi mapy albo granicę, której nie ma —
 * i jedno, i drugie wygląda na artefakt renderowania, a nie na pomyłkę w indeksie.
 */
describe('isBorderTile', () => {
  /** Rysunek na tablicę właścicieli: kropka to pustkowie, `~` woda, cyfra to slot. */
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

  it('obrysowuje styk z pustkowiem, wodą i cudzym terytorium', () => {
    const { owner, width, height } = draw([
      '.....',
      '.111.',
      '.111~',
      '.1122',
      '.....',
    ]);

    const border = (x: number, y: number) => isBorderTile(owner, width, height, x, y);

    // Środek własnego terytorium — jedyny kafelek otoczony wyłącznie sobą.
    expect(border(2, 2)).toBe(false);

    expect(border(1, 1)).toBe(true); // pustkowie z lewej i z góry
    expect(border(3, 2)).toBe(true); // woda z prawej
    expect(border(2, 3)).toBe(true); // sąsiad slotu 2
    expect(border(3, 3)).toBe(true); // slot 2 od strony slotu 1
  });

  it('domyka obrys na krawędzi mapy', () => {
    const { owner, width, height } = draw(['11', '11']);

    // Cała mapa należy do jednego gracza, więc gdyby krawędź nie liczyła się jako obcy
    // sąsiad, państwo nie miałoby ani jednego kafelka granicznego.
    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) {
        expect(isBorderTile(owner, width, height, x, y)).toBe(true);
      }
    }
  });

  it('nie liczy zawinięcia wiersza za sąsiedztwo', () => {
    const { owner, width, height } = draw([
      '11..1',
      '11...',
      '11...',
    ]);

    // Kafelek (0, 1) ma z trzech stron siebie samego, a z czwartej krawędź mapy. Jego
    // „lewym sąsiadem" w płaskiej tablicy jest kafelek (4, 0) — też slot 1 — więc bez
    // sprawdzenia krawędzi wyszłoby, że to środek państwa, i cała lewa kolumna zostałaby
    // bez obrysu. Mapa jest prostokątem, nie torusem.
    expect(isBorderTile(owner, width, height, 0, 1)).toBe(true);
  });

  it('nie obrysowuje pustkowia ani wody', () => {
    const { owner, width, height } = draw(['.1.', '.~.']);

    expect(isBorderTile(owner, width, height, 0, 0)).toBe(false);
    expect(isBorderTile(owner, width, height, 1, 1)).toBe(false);
  });
});
