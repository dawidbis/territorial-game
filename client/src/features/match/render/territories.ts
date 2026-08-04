/**
 * Miara terytoriów: gdzie postawić podpis państwa i jaki kadr je obejmuje.
 *
 * Jedno przejście po tablicy właścicieli liczy wszystko naraz — środek ciężkości, prostokąt
 * obejmujący i najdłuższy poziomy ciąg kafelków. Trzy przejścia byłyby trzy razy droższe,
 * a przy dwóch milionach kafelków to jest różnica, którą widać na wykresie klatek.
 *
 * Osobno od renderera i bez jednego odwołania do kanwy, bo to jest czysta arytmetyka na
 * tablicy — a taka daje się przetestować bez przeglądarki.
 */
import { WASTELAND_OWNER, WATER_OWNER } from './palette';

/** Ile jest slotów właścicieli — 256 z definicji (D12). */
const SLOT_COUNT = 256;

export interface Territory {
  tiles: number;

  /**
   * Kafelek, na którym siada podpis — **zawsze wewnątrz terytorium**.
   *
   * Zwykle jest to środek ciężkości. Państwo w kształcie podkowy albo półksiężyca ma go
   * jednak poza sobą, a nick wypisany na cudzej ziemi jest gorszy niż nick postawiony
   * niesymetrycznie — wtedy zamiast niego bierzemy środek najdłuższego poziomego ciągu
   * własnych kafelków, który z definicji leży na własnym terenie i w szerokiej części
   * państwa.
   */
  anchorX: number;
  anchorY: number;

  /** Prostokąt obejmujący, w kafelkach. Domknięty z obu stron. */
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
}

/**
 * Mierzy wszystkie terytoria naraz.
 *
 * @returns tablica indeksowana slotem; `null` znaczy „ten slot nie ma ani kafelka".
 */
export function measureTerritories(
  owner: Uint8Array,
  width: number,
  height: number,
): readonly (Territory | null)[] {
  const tiles = new Uint32Array(SLOT_COUNT);
  const sumX = new Float64Array(SLOT_COUNT);
  const sumY = new Float64Array(SLOT_COUNT);

  const minX = new Int32Array(SLOT_COUNT).fill(width);
  const minY = new Int32Array(SLOT_COUNT).fill(height);
  const maxX = new Int32Array(SLOT_COUNT).fill(-1);
  const maxY = new Int32Array(SLOT_COUNT).fill(-1);

  const runLength = new Uint32Array(SLOT_COUNT);
  const runX = new Uint32Array(SLOT_COUNT);
  const runY = new Uint32Array(SLOT_COUNT);

  const closeRun = (slot: number, from: number, to: number, y: number) => {
    if (slot === WASTELAND_OWNER || slot === WATER_OWNER) {
      return;
    }

    const length = to - from + 1;

    if (length <= runLength[slot]) {
      return;
    }

    runLength[slot] = length;
    runX[slot] = (from + to) >> 1;
    runY[slot] = y;
  };

  let index = 0;

  for (let y = 0; y < height; y++) {
    let runSlot = owner[index];
    let runStart = 0;

    for (let x = 0; x < width; x++, index++) {
      const slot = owner[index];

      if (slot !== runSlot) {
        closeRun(runSlot, runStart, x - 1, y);

        runSlot = slot;
        runStart = x;
      }

      if (slot === WASTELAND_OWNER || slot === WATER_OWNER) {
        continue;
      }

      tiles[slot]++;
      sumX[slot] += x;
      sumY[slot] += y;

      if (x < minX[slot]) {
        minX[slot] = x;
      }

      if (x > maxX[slot]) {
        maxX[slot] = x;
      }

      if (y < minY[slot]) {
        minY[slot] = y;
      }

      if (y > maxY[slot]) {
        maxY[slot] = y;
      }
    }

    closeRun(runSlot, runStart, width - 1, y);
  }

  const result: (Territory | null)[] = new Array(SLOT_COUNT).fill(null);

  for (let slot = 0; slot < SLOT_COUNT; slot++) {
    if (tiles[slot] === 0) {
      continue;
    }

    const centerX = Math.round(sumX[slot] / tiles[slot]);
    const centerY = Math.round(sumY[slot] / tiles[slot]);

    const inside = owner[centerY * width + centerX] === slot;

    result[slot] = {
      tiles: tiles[slot],
      anchorX: inside ? centerX : runX[slot],
      anchorY: inside ? centerY : runY[slot],
      minX: minX[slot],
      minY: minY[slot],
      maxX: maxX[slot],
      maxY: maxY[slot],
    };
  }

  return result;
}
