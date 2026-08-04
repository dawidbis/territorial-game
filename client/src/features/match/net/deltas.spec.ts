import { describe, expect, it } from 'vitest';

import { applyOwnershipDeltas } from './deltas';

// Dekodowanie delt jest lustrem kodowania po stronie serwera (`build_snapshot`). Błąd
// o jeden w tym miejscu wygląda jak porozrzucane kafelki, nie jak awaria — więc test
// wypisuje oczekiwania ręcznie, zamiast liczyć je drugą implementacją tego samego.
describe('applyOwnershipDeltas', () => {
  it('czyta pierwszy indeks bezwzględnie, a resztę jako różnice', () => {
    const owner = new Uint8Array(20);

    const touched = applyOwnershipDeltas([{ slot: 3, indexDeltas: [5, 2, 1] }], owner);

    // Indeksy 5, 7 i 8 — szóstka zostaje nietknięta, bo różnica 2 ją przeskakuje.
    expect(touched).toEqual([5, 7, 8]);
    expect(Array.from(owner.slice(4, 10))).toEqual([0, 3, 0, 3, 3, 0]);
  });

  it('trzyma osobny kursor dla każdej grupy', () => {
    const owner = new Uint8Array(20);

    // Grupy są niezależne: druga nie kontynuuje numeracji pierwszej. Wspólny kursor
    // przesunąłby całe terytorium drugiego gracza o pozycję pierwszego.
    const touched = applyOwnershipDeltas(
      [
        { slot: 1, indexDeltas: [10, 1] },
        { slot: 2, indexDeltas: [2, 1] },
      ],
      owner,
    );

    expect(touched).toEqual([10, 11, 2, 3]);
    expect(owner[10]).toBe(1);
    expect(owner[11]).toBe(1);
    expect(owner[2]).toBe(2);
    expect(owner[3]).toBe(2);
  });

  it('przepisuje kafelek na nowego właściciela', () => {
    const owner = new Uint8Array([7, 7, 7, 7]);

    applyOwnershipDeltas([{ slot: 9, indexDeltas: [1, 2] }], owner);

    expect(Array.from(owner)).toEqual([7, 9, 7, 9]);
  });

  it('pomija indeksy spoza mapy zamiast pisać poza tablicą', () => {
    const owner = new Uint8Array(4);

    const touched = applyOwnershipDeltas([{ slot: 1, indexDeltas: [2, 100] }], owner);

    expect(touched).toEqual([2]);
    expect(Array.from(owner)).toEqual([0, 0, 1, 0]);
  });

  it('nie rusza niczego, gdy grupa jest pusta', () => {
    const owner = new Uint8Array([5, 5]);

    expect(applyOwnershipDeltas([{ slot: 1, indexDeltas: [] }], owner)).toEqual([]);
    expect(Array.from(owner)).toEqual([5, 5]);
  });
});
