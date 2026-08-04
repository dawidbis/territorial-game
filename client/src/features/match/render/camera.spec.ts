import { describe, expect, it } from 'vitest';

import { Camera } from './camera';

/**
 * Testy kamery.
 *
 * Cała ta klasa to jedno przeliczenie kafelka na piksel i z powrotem, a błąd o jeden
 * w takim przeliczeniu nie wygląda jak błąd — wygląda jak „mapa dziwnie się przesuwa".
 * Dlatego sprawdzane są własności, a nie konkretne liczby: kafelek pod kursorem ma pod nim
 * zostać, a widok nigdy nie ma pokazywać pustki obok mapy.
 */
describe('Camera', () => {
  const MAP = { width: 2000, height: 1000 };
  const VIEW = { width: 1000, height: 500 };

  const fresh = () => new Camera(MAP.width, MAP.height, VIEW);

  it('startuje z całą mapą w kadrze', () => {
    const source = fresh().source();

    expect(source.width).toBeCloseTo(MAP.width);
    expect(source.height).toBeCloseTo(MAP.height);
    expect(source.x).toBeCloseTo(0);
    expect(source.y).toBeCloseTo(0);
  });

  it('nie oddala się poniżej całej mapy', () => {
    const camera = fresh();

    camera.zoomAt(0.01, 500, 250);

    const source = camera.source();

    expect(source.width).toBeLessThanOrEqual(MAP.width + 0.001);
    expect(source.height).toBeLessThanOrEqual(MAP.height + 0.001);
  });

  // Sedno klasy: przy zoomie do środka widoku mapa ucieka spod kursora i nawigacja
  // zamienia się w walkę. Punkt zaczepienia musi zostać na miejscu.
  it('trzyma kafelek pod kursorem przy przybliżaniu', () => {
    const camera = fresh();
    const [x, y] = [300, 120];

    const before = camera.toTile(x, y);

    camera.zoomAt(2.5, x, y);

    const after = camera.toTile(x, y);

    expect(after.x).toBeCloseTo(before.x, 6);
    expect(after.y).toBeCloseTo(before.y, 6);
  });

  /**
   * Wyjazd poza mapę jest **celowy**, a jego granica jest tym, co go odróżnia od zgubienia
   * mapy z oczu. Bez zapasu terytorium przy krawędzi świata da się oglądać wyłącznie
   * przyklejone do brzegu ekranu; z zapasem bez granicy da się odjechać w pustkę — i to
   * nie jest teoria, tylko pierwsza wersja tego kodu, w której przy dużym przybliżeniu
   * cały ekran robił się czarny.
   */
  it('pozwala wyprowadzić róg mapy na środek kadru i ani piksela dalej', () => {
    const camera = fresh();

    camera.zoomAt(4, 500, 250);
    camera.panBy(100000, 100000);

    const source = camera.source();

    // Lewy górny róg mapy stoi dokładnie w środku ekranu.
    expect(source.x + source.width / 2).toBeCloseTo(0);
    expect(source.y + source.height / 2).toBeCloseTo(0);

    camera.panBy(-200000, -200000);

    const far = camera.source();

    expect(far.x + far.width / 2).toBeCloseTo(MAP.width);
    expect(far.y + far.height / 2).toBeCloseTo(MAP.height);
  });

  it('nigdy nie gubi mapy z ekranu, niezależnie od przybliżenia', () => {
    for (const zoom of [2, 8, 40, 1000]) {
      const camera = fresh();

      camera.zoomAt(zoom, 500, 250);
      camera.panBy(100000, 100000);

      const source = camera.source();

      // Połowa kadru to wciąż mapa — bez tego gracz zostaje na czarnym ekranie bez
      // wskazówki, w którą stronę wracać.
      expect(source.x + source.width).toBeGreaterThan(source.width / 2 - 0.001);
      expect(source.y + source.height).toBeGreaterThan(source.height / 2 - 0.001);
    }
  });

  it('nie przekracza trzeciej części mapy nawet przy oddaleniu', () => {
    const camera = fresh();

    // Ledwie ciaśniej niż pełne oddalenie: połowa kadru to wtedy więcej niż trzecia część
    // mapy, więc o zapasie decyduje sufit, a nie kadr.
    camera.zoomAt(1.2, 500, 250);
    camera.panBy(100000, 0);

    const source = camera.source();

    expect(source.x).toBeGreaterThanOrEqual(-MAP.width / 3 - 0.001);
  });

  it('mieści całe terytorium w kadrze i staje na jego środku', () => {
    const camera = fresh();

    camera.fit(100, 100, 140, 120);

    const source = camera.source();

    // Obietnica przycisku brzmi „zobaczysz CAŁE swoje terytorium", więc prostokąt ma się
    // zmieścić w kadrze z każdej strony, a nie tylko wpaść do niego środkiem.
    expect(source.x).toBeLessThanOrEqual(100);
    expect(source.y).toBeLessThanOrEqual(100);
    expect(source.x + source.width).toBeGreaterThanOrEqual(141);
    expect(source.y + source.height).toBeGreaterThanOrEqual(121);

    expect(source.x + source.width / 2).toBeCloseTo(120.5);
    expect(source.y + source.height / 2).toBeCloseTo(110.5);
  });

  it('nie przybliża na pojedynczym kafelku bardziej, niż wolno zoomowi', () => {
    const camera = fresh();

    camera.zoomAt(1000, 500, 250);

    const tightest = camera.tileSize;

    camera.fit(500, 500, 500, 500);

    // Państwo z jednym kafelkiem kazałoby dobrać skalę kilkusetkrotną — a wtedy kadr
    // pokazuje jeden kwadrat i nic poza nim.
    expect(camera.tileSize).toBeLessThanOrEqual(tightest + 0.001);
  });

  it('środkuje oś, w której mapa jest węższa niż kadr', () => {
    // Kadr o proporcjach innych niż mapa: przy pełnym oddaleniu jedna oś ma zapas.
    const camera = new Camera(MAP.width, MAP.height, { width: 1000, height: 1000 });

    camera.panBy(0, -100000);

    const source = camera.source();

    // Dosunięcie do krawędzi zostawiłoby pustą połowę ekranu i wyglądało jak błąd
    // renderowania — więc oś z zapasem ma być wyśrodkowana, a nie dociśnięta.
    expect(source.y + source.height / 2).toBeCloseTo(MAP.height / 2);
  });

  it('po zmniejszeniu okna nie pokazuje pustki obok mapy', () => {
    const camera = fresh();

    camera.resize({ width: 4000, height: 2000 });

    const source = camera.source();

    expect(source.width).toBeLessThanOrEqual(MAP.width + 0.001);
    expect(source.height).toBeLessThanOrEqual(MAP.height + 0.001);
  });
});
