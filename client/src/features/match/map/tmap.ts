/**
 * Czytnik pliku terenu `.tmap` — druga implementacja tego samego formatu, obok `gs::tmap`.
 *
 * Dwie implementacje jednego formatu to zwykle proszenie się o rozjazd, ale tutaj nie ma
 * wyjścia: jedna strona jest w C++, druga w przeglądarce. Zamiast liczyć na dyscyplinę,
 * ten czytnik **sprawdza wszystko, co potrafi** i porównuje wynik z tym, co powiedział
 * serwer w `MatchInit`. Rozjazd formatu kończy się więc komunikatem przy wczytaniu, a nie
 * przesuniętym kontynentem w dwunastej minucie meczu.
 *
 * Sekcji z punktami startowymi ten kod **nie czyta w ogóle** — i po to właśnie offset terenu
 * stoi wprost w nagłówku. Klient nie musi wiedzieć o istnieniu sekcji, których nie używa.
 */

/** Kody terenu; te same liczby, co w `gs::tmap::Terrain`. */
export const Terrain = {
  water: 0,
  lowlands: 1,
  highlands: 2,
  mountains: 3,
} as const;

export const TERRAIN_TYPE_COUNT = 4;

const MAGIC = 'TMAP';
const FORMAT_VERSION = 1;
const HEADER_SIZE = 20;

export interface TerrainMap {
  id: string;
  width: number;
  height: number;
  /** `width * height` bajtów, wiersz po wierszu. Widok w oryginalny bufor, bez kopii. */
  terrain: Uint8Array;
}

export class TmapError extends Error {}

/**
 * Czyta plik z pamięci.
 *
 * @throws {TmapError} gdy plik nie jest `.tmap`, jest z innej wersji formatu albo jest
 * krótszy, niż zapowiada jego własny nagłówek. Każdy z tych plików wczytałby się bez
 * awarii i zepsuł mecz później.
 */
export function decodeTmap(buffer: ArrayBuffer): TerrainMap {
  if (buffer.byteLength < HEADER_SIZE) {
    throw new TmapError(
      `Plik terenu ma ${buffer.byteLength} B, a sam nagłówek zajmuje ${HEADER_SIZE} B.`,
    );
  }

  const view = new DataView(buffer);
  const bytes = new Uint8Array(buffer);

  const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);

  if (magic !== MAGIC) {
    throw new TmapError(`Plik terenu nie zaczyna się od sygnatury ${MAGIC}.`);
  }

  // Wszystkie liczby są little-endian — stąd `true` w każdym odczycie.
  const version = view.getUint16(4, true);

  if (version !== FORMAT_VERSION) {
    throw new TmapError(
      `Plik terenu jest w wersji ${version}, a ten klient czyta wersję ${FORMAT_VERSION}.`,
    );
  }

  const width = view.getUint16(6, true);
  const height = view.getUint16(8, true);
  const terrainTypes = bytes[10];
  const idLength = bytes[11];
  const terrainOffset = view.getUint32(16, true);

  if (terrainTypes !== TERRAIN_TYPE_COUNT) {
    throw new TmapError(
      `Plik terenu zna ${terrainTypes} typów terenu, a ten klient ${TERRAIN_TYPE_COUNT}. ` +
        'Paleta rozjechałaby się po cichu.',
    );
  }

  if (width === 0 || height === 0) {
    throw new TmapError(`Plik terenu podaje wymiary ${width}×${height}.`);
  }

  const tiles = width * height;

  if (terrainOffset + tiles > buffer.byteLength) {
    throw new TmapError(
      `Plik terenu zapowiada ${tiles} kafelków od offsetu ${terrainOffset}, ` +
        `a ma tylko ${buffer.byteLength} B.`,
    );
  }

  const id = new TextDecoder('ascii').decode(bytes.subarray(HEADER_SIZE, HEADER_SIZE + idLength));

  return { id, width, height, terrain: new Uint8Array(buffer, terrainOffset, tiles) };
}

/** Suma kontrolna pliku w postaci szesnastkowej — ta sama, którą niesie `MatchInit`. */
export async function sha256Hex(buffer: ArrayBuffer): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', buffer);

  return toHex(new Uint8Array(digest));
}

export function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}
