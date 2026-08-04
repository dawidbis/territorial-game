import { describe, expect, it } from 'vitest';

import { decodeTmap, TmapError } from './tmap';

/**
 * Testy czytnika `.tmap`.
 *
 * Pliki budowane są **bajt po bajt**, bo o uszkodzony nagłówek nie da się poprosić
 * konwertera — a to właśnie uszkodzone pliki są tu przedmiotem testu. Format ma dwie
 * niezależne implementacje (ta i `gs::tmap`), więc każdy odrzucony przypadek to jedna
 * droga mniej do rozjazdu, który wyszedłby dopiero jako przesunięty kontynent.
 */
describe('decodeTmap', () => {
  const HEADER_SIZE = 20;

  function build(
    options: {
      magic?: string;
      version?: number;
      width?: number;
      height?: number;
      terrainTypes?: number;
      id?: string;
      truncateTo?: number;
    } = {},
  ): ArrayBuffer {
    const {
      magic = 'TMAP',
      version = 1,
      width = 4,
      height = 3,
      terrainTypes = 4,
      id = 'test',
    } = options;

    const terrainOffset = HEADER_SIZE + id.length;
    const total = terrainOffset + width * height;

    const buffer = new ArrayBuffer(total);
    const view = new DataView(buffer);
    const bytes = new Uint8Array(buffer);

    for (let i = 0; i < 4; i++) {
      bytes[i] = magic.charCodeAt(i);
    }

    view.setUint16(4, version, true);
    view.setUint16(6, width, true);
    view.setUint16(8, height, true);
    bytes[10] = terrainTypes;
    bytes[11] = id.length;
    view.setUint16(12, 0, true);
    view.setUint32(16, terrainOffset, true);

    for (let i = 0; i < id.length; i++) {
      bytes[HEADER_SIZE + i] = id.charCodeAt(i);
    }

    for (let i = 0; i < width * height; i++) {
      bytes[terrainOffset + i] = i % 4;
    }

    return options.truncateTo === undefined ? buffer : buffer.slice(0, options.truncateTo);
  }

  it('czyta identyfikator, wymiary i teren', () => {
    const map = decodeTmap(build({ id: 'synthetic', width: 4, height: 3 }));

    expect(map.id).toBe('synthetic');
    expect(map.width).toBe(4);
    expect(map.height).toBe(3);
    expect(map.terrain.length).toBe(12);
    expect(Array.from(map.terrain.slice(0, 5))).toEqual([0, 1, 2, 3, 0]);
  });

  it('odrzuca plik bez sygnatury', () => {
    expect(() => decodeTmap(build({ magic: 'PNG\0' }))).toThrow(TmapError);
  });

  it('odrzuca plik z innej wersji formatu', () => {
    expect(() => decodeTmap(build({ version: 2 }))).toThrow(TmapError);
  });

  // Inna liczba typów terenu znaczy inną paletę po drugiej stronie. Bez tego sprawdzenia
  // mapa wczytałaby się i pomalowała góry kolorem, którego nikt nie przewidział.
  it('odrzuca plik z inną liczbą typów terenu', () => {
    expect(() => decodeTmap(build({ terrainTypes: 5 }))).toThrow(TmapError);
  });

  it('odrzuca plik krótszy, niż zapowiada jego własny nagłówek', () => {
    expect(() => decodeTmap(build({ truncateTo: 25 }))).toThrow(TmapError);
  });

  it('odrzuca plik krótszy niż nagłówek', () => {
    expect(() => decodeTmap(new ArrayBuffer(8))).toThrow(TmapError);
  });
});
