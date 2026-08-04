/** Grupa zmian własności: jeden właściciel i lista indeksów kodowana różnicami. */
export interface DeltaGroup {
  slot: number;
  indexDeltas: readonly number[];
}

/**
 * Nanosi delty na tablicę właścicieli i oddaje dotknięte indeksy.
 *
 * Lustro kodowania z serwera (`build_snapshot`, D5): grupa płaci za właściciela raz,
 * a indeksy w niej jadą różnicami — **pierwszy bezwzględnie**, bo nie ma od czego liczyć
 * różnicy. Ekspansja jest przestrzennie ciągła, więc różnice mieszczą się w jednym bajcie
 * varinta i cała mapa jedzie tanio.
 *
 * Osobna funkcja, a nie metoda workera, wyłącznie po to, żeby dała się przetestować: błąd
 * o jeden w tym miejscu wygląda jak porozrzucane kafelki, a nie jak awaria, i wyszedłby
 * dopiero w rozgrywce.
 */
export function applyOwnershipDeltas(
  groups: readonly DeltaGroup[],
  owner: Uint8Array,
): number[] {
  const touched: number[] = [];

  for (const group of groups) {
    let index = 0;

    for (let position = 0; position < group.indexDeltas.length; position++) {
      index = position === 0 ? group.indexDeltas[0] : index + group.indexDeltas[position];

      // Indeks spoza mapy znaczy rozjechany strumień delt. Cicho pominięty jest lepszy niż
      // zapis poza tablicą, ale i tak nie ma prawa się zdarzyć — stąd warunek, nie modulo.
      if (index < owner.length) {
        owner[index] = group.slot;
        touched.push(index);
      }
    }
  }

  return touched;
}
