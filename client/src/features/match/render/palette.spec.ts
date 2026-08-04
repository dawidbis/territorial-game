import { describe, expect, it } from 'vitest';

import { TERRAIN_TYPE_COUNT, Terrain } from '../map/tmap';
import { buildPalette, PALETTE_STRIDE, WATER_OWNER } from './palette';

/**
 * Testy palety.
 *
 * Sprawdzane są własności widoczne na ekranie, a nie konkretne bajty: że teren pod
 * kolorem gracza wciąż widać, że woda zostaje wodą i że kanały nie są zamienione.
 * Ostatnie jest najpodstępniejsze — mapa z zamienionym R i B wygląda na działającą.
 */
describe('buildPalette', () => {
  const at = (palette: Uint32Array, owner: number, terrain: number) =>
    palette[owner * TERRAIN_TYPE_COUNT + terrain];

  /** Ten sam wpis, ale z piętra otoczki. */
  const edge = (palette: Uint32Array, owner: number, terrain: number) =>
    palette[PALETTE_STRIDE + owner * TERRAIN_TYPE_COUNT + terrain];

  /** Rozkłada piksel na kanały niezależnie od kolejności bajtów tej maszyny. */
  const channels = (pixel: number) => {
    const bytes = new Uint8Array(new Uint32Array([pixel]).buffer);

    return { red: bytes[0], green: bytes[1], blue: bytes[2], alpha: bytes[3] };
  };

  it('maluje slot jego własnym kolorem, bez zamiany kanałów', () => {
    const palette = buildPalette([{ slot: 1, colorRgb: 0xff0000 }]);

    const fill = channels(at(palette, 1, Terrain.lowlands));

    // Wypełnienie jest wymieszane z terenem, więc czerwień nie jest już pełna — ale wciąż
    // musi być kanałem dominującym. Zamiana R z B daje obraz, który *wygląda* na działający,
    // i to jest jedyny błąd tej tabeli, którego nie widać gołym okiem.
    expect(fill.red).toBeGreaterThan(fill.green);
    expect(fill.red).toBeGreaterThan(fill.blue);
    expect(fill.alpha).toBe(255);

    const border = channels(edge(palette, 1, Terrain.lowlands));

    // Otoczka to kolor gracza rozjaśniony bielą, więc kanał pełny zostaje pełny,
    // a dwa pozostałe podnoszą się o tyle samo.
    expect(border.red).toBe(255);
    expect(border.green).toBe(border.blue);
    expect(border.green).toBeGreaterThan(0);
    expect(border.alpha).toBe(255);
  });

  /**
   * Sedno zmiany, przez którą ten test wygląda inaczej niż jego poprzednik.
   *
   * Wcześniej kafelek dostawał **czysty** kolor właściciela przyciemniony wysokością terenu
   * i po kilku minutach meczu połowa mapy była płaską plamą: gracz nie widział ani gór,
   * których nie opłaca się atakować, ani przesmyków, którymi warto iść. Teraz kolor gracza
   * jest półprzezroczysty, więc ukształtowanie czyta się pod terytorium **tak samo** jak
   * na pustkowiu — jaśniej znaczy wyżej, wszędzie.
   */
  it('zostawia teren widoczny pod kolorem gracza', () => {
    const palette = buildPalette([{ slot: 3, colorRgb: 0x3366cc }]);

    const lowlands = luma(at(palette, 3, Terrain.lowlands));
    const highlands = luma(at(palette, 3, Terrain.highlands));
    const mountains = luma(at(palette, 3, Terrain.mountains));

    expect(highlands).toBeGreaterThan(lowlands);
    expect(mountains).toBeGreaterThan(highlands);

    // Różnica ma być widoczna, a nie tylko dodatnia: przy zbyt gęstym kolorze gracza teren
    // formalnie prześwituje, a na ekranie go nie ma.
    expect(mountains - lowlands).toBeGreaterThan(10);
  });

  it('odróżnia otoczkę od wypełnienia na tyle, żeby granicę było widać', () => {
    // Ciemny i jasny kolor gracza, bo najciaśniej robi się w dwóch różnych miejscach:
    // ciemnemu wypełnienie rozjaśniają góry, jasnemu brakuje już miejsca do bieli.
    for (const colorRgb of [0x3366cc, 0xffcc33]) {
      const palette = buildPalette([{ slot: 7, colorRgb }]);

      for (const terrain of [Terrain.lowlands, Terrain.highlands, Terrain.mountains]) {
        const fill = at(palette, 7, terrain);
        const border = edge(palette, 7, terrain);

        expect(border).not.toBe(fill);

        // Otoczka jest **jaśniejsza**, bo granica ma odskakiwać także od własnego środka,
        // nie tylko od sąsiada w podobnym kolorze.
        expect(luma(border)).toBeGreaterThan(luma(fill) + 20);
      }
    }
  });

  it('nie daje otoczki pustkowiu ani wodzie', () => {
    const palette = buildPalette([{ slot: 2, colorRgb: 0x00ff00 }]);

    // Renderer po te wpisy nie sięga, ale tabela ma być kompletna: dziura w niej to czarny
    // kafelek na mapie, gdyby kiedykolwiek sięgnął.
    expect(edge(palette, 0, Terrain.lowlands)).toBe(at(palette, 0, Terrain.lowlands));
    expect(edge(palette, WATER_OWNER, Terrain.water)).toBe(at(palette, WATER_OWNER, Terrain.water));
  });

  it('trzyma wodę wodą niezależnie od właściciela', () => {
    const palette = buildPalette([{ slot: 5, colorRgb: 0xff00ff }]);

    const unowned = at(palette, WATER_OWNER, Terrain.water);

    // Nawet gdyby run objął kafelek wody, terytorium nie ma prawa wylać się na morze.
    expect(at(palette, 5, Terrain.water)).toBe(unowned);
    expect(at(palette, 0, Terrain.water)).toBe(unowned);
  });

  it('daje pustkowiu inny kolor niż wodzie, żeby było widać brzeg', () => {
    const palette = buildPalette([]);

    expect(at(palette, 0, Terrain.lowlands)).not.toBe(at(palette, 0, Terrain.water));
  });

  /** Luminancja postrzegana w skali 0–255. */
  const luma = (pixel: number) => {
    const { red, green, blue } = channels(pixel);

    return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
  };

  /**
   * Pierwsza wersja palety miała luminancję lądu rzędu 40 i mapa czytała się jak zdjęcie
   * nocne. Przy kilku procentach zajętego terenu gracz przez większość meczu patrzy właśnie
   * na pustkowie, więc to ono decyduje, czy widać, gdzie jest ląd. Próg jest tu po to, żeby
   * przypadkowe przyciemnienie nie przeszło niezauważone — a świadome wymagało zmiany testu.
   */
  it('trzyma pustkowie w jasności, przy której mapa nie jest nocna', () => {
    const palette = buildPalette([]);

    for (const terrain of [Terrain.lowlands, Terrain.highlands, Terrain.mountains]) {
      expect(luma(at(palette, 0, terrain))).toBeGreaterThan(90);
    }

    // Woda ma zostać wyraźnie ciemniejsza od lądu — na tym kontraście czyta się linia brzegowa.
    expect(luma(at(palette, 0, Terrain.water))).toBeLessThan(
      luma(at(palette, 0, Terrain.lowlands)) - 30,
    );
  });

  it('rozjaśnia pustkowie wraz z wysokością terenu', () => {
    const palette = buildPalette([]);

    // Odwrotnie niż przy kafelkach zajętych, gdzie teren PRZYCIEMNIA kolor właściciela.
    // Tu chodzi o odczytanie ukształtowania, tam o to, żeby nie zgubić go pod kolorem gracza.
    expect(luma(at(palette, 0, Terrain.lowlands))).toBeLessThan(
      luma(at(palette, 0, Terrain.highlands)),
    );
    expect(luma(at(palette, 0, Terrain.highlands))).toBeLessThan(
      luma(at(palette, 0, Terrain.mountains)),
    );
  });

  it('nie zostawia slotu bez koloru, nawet jeśli nie ma go w obsadzie', () => {
    const palette = buildPalette([{ slot: 1, colorRgb: 0x00ff00 }]);

    // Slot spoza MatchInit nie ma prawa dać czarnej dziury w mapie.
    expect(channels(at(palette, 200, Terrain.lowlands)).alpha).toBe(255);
  });
});
