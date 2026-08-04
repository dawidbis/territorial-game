/**
 * Kamera: przesuwanie i skala, czyli jedyna matematyka między kafelkiem a pikselem.
 *
 * Osobno od renderera i bez jednego odwołania do kanwy — dzięki temu całość testuje się
 * bez przeglądarki, a błąd o jeden w przeliczeniu przestaje być czymś, co widać dopiero
 * jako „mapa ucieka spod kursora przy zoomie".
 */
export interface Viewport {
  /** Rozmiar kanwy w pikselach urządzenia, nie w pikselach CSS. */
  width: number;
  height: number;
}

/** Wycinek mapy do przepisania na kanwę — argumenty `drawImage` w kafelkach. */
export interface SourceRect {
  x: number;
  y: number;
  width: number;
  height: number;
}

/** Najciaśniejszy zoom: pojedynczy kafelek na 24 piksele. Dalej widać już tylko kwadraty. */
const MAX_SCALE = 24;

/**
 * Górny sufit wyjazdu za mapę — ułamek jej rozmiaru w tej osi.
 *
 * Bez zapasu terytorium przy krawędzi świata da się oglądać wyłącznie przyklejone do brzegu
 * ekranu: widok stoi na krawędzi mapy, a rozkazy wydaje się w pasie kilku pikseli.
 *
 * **Sam ten sufit nie wystarczy** i pierwsza wersja właśnie na tym poległa: jedna trzecia
 * mapy to przy dużym przybliżeniu wielokrotność kadru, więc dało się wyjechać tak daleko,
 * że na ekranie nie było już ani kafelka mapy — sama pustka i żadnej wskazówki, w którą
 * stronę wracać. Drugie ograniczenie jest w {@link Camera.between}.
 */
const OVERSCROLL = 1 / 3;

/**
 * Zapas dookoła terytorium przy dopasowywaniu kadru — patrz {@link fit}.
 *
 * Państwo dociśnięte do krawędzi kadru wygląda na przycięte i nie widać, co je otacza,
 * a to jest pierwsza rzecz, po którą gracz sięga wzrokiem po wyśrodkowaniu.
 */
const FIT_MARGIN = 1.25;

export class Camera {
  /** Środek widoku w kafelkach. Ułamki są celowe — bez nich przesuwanie skacze przy zoomie. */
  private centerX: number;
  private centerY: number;

  /** Pikseli urządzenia na kafelek. */
  private scale = 1;

  constructor(
    private readonly mapWidth: number,
    private readonly mapHeight: number,
    private viewport: Viewport,
  ) {
    this.centerX = mapWidth / 2;
    this.centerY = mapHeight / 2;
    this.scale = this.minScale();
    this.clamp();
  }

  /** Skala, przy której cała mapa mieści się w widoku — i zarazem najdalszy dozwolony zoom. */
  private minScale(): number {
    return Math.min(this.viewport.width / this.mapWidth, this.viewport.height / this.mapHeight);
  }

  get tileSize(): number {
    return this.scale;
  }

  resize(viewport: Viewport) {
    this.viewport = viewport;

    // Skala mogła być dozwolona przy poprzednim rozmiarze, a przy nowym pokazywać pustkę
    // obok mapy. Podniesienie jej tutaj jest jedynym momentem, w którym da się to złapać.
    this.scale = Math.max(this.scale, this.minScale());
    this.clamp();
  }

  /** Przesuwa widok o podany wektor **w pikselach ekranu** — tyle, ile przejechał kursor. */
  panBy(dxPixels: number, dyPixels: number) {
    this.centerX -= dxPixels / this.scale;
    this.centerY -= dyPixels / this.scale;
    this.clamp();
  }

  /**
   * Zoom wokół punktu ekranu — kafelek pod kursorem ma pod nim zostać.
   *
   * To jest cała trudność tej klasy: przy zoomie do środka widoku mapa ucieka spod
   * kursora i nawigacja staje się walką. Punkt zaczepienia liczony jest **przed** zmianą
   * skali i odtwarzany po niej.
   */
  zoomAt(factor: number, screenX: number, screenY: number) {
    const before = this.toTile(screenX, screenY);

    this.scale = Math.min(MAX_SCALE, Math.max(this.minScale(), this.scale * factor));

    const after = this.toTile(screenX, screenY);

    this.centerX += before.x - after.x;
    this.centerY += before.y - after.y;
    this.clamp();
  }

  /**
   * Ustawia widok na wskazanym kafelku i dobiera skalę tak, żeby objąć zadaną liczbę
   * kafelków w krótszej osi kadru.
   *
   * Bez tego gracz po wejściu do meczu ogląda całą mapę, na której jego pięćdziesiąt dwa
   * kafelki są plamką kilku pikseli — i pierwszą czynnością w grze jest szukanie samego
   * siebie. Skala przechodzi przez te same ograniczenia co zoom, więc na małej mapie
   * „przybliż na start" nie może pokazać pustki obok niej.
   */
  focus(tileX: number, tileY: number, tilesVisible: number) {
    const wanted = Math.min(this.viewport.width, this.viewport.height) / Math.max(1, tilesVisible);

    this.scale = Math.min(MAX_SCALE, Math.max(this.minScale(), wanted));

    this.centerX = tileX;
    this.centerY = tileY;

    this.clamp();
  }

  /**
   * Ustawia kadr tak, żeby objął cały wskazany prostokąt kafelków.
   *
   * Środkiem jest środek prostokąta, a nie środek ciężkości terytorium: przy państwie
   * w kształcie podkowy te dwa punkty leżą gdzie indziej, a obietnicą przycisku jest
   * „zobaczysz **całe** swoje terytorium" — więc wygrywa ten punkt, który to gwarantuje.
   *
   * Skala przechodzi przez te same ograniczenia co zoom, więc pojedynczy kafelek nie
   * wystrzeli widoku poza najciaśniejsze przybliżenie.
   */
  fit(minX: number, minY: number, maxX: number, maxY: number) {
    const width = (maxX - minX + 1) * FIT_MARGIN;
    const height = (maxY - minY + 1) * FIT_MARGIN;

    const wanted = Math.min(this.viewport.width / width, this.viewport.height / height);

    this.scale = Math.min(MAX_SCALE, Math.max(this.minScale(), wanted));

    this.centerX = (minX + maxX + 1) / 2;
    this.centerY = (minY + maxY + 1) / 2;

    this.clamp();
  }

  /** Kafelek pod punktem ekranu. Poza mapą zwraca wartości spoza zakresu — to zadanie wołającego. */
  toTile(screenX: number, screenY: number): { x: number; y: number } {
    const source = this.source();

    return {
      x: source.x + screenX / this.scale,
      y: source.y + screenY / this.scale,
    };
  }

  /** Wycinek mapy widoczny w tej chwili. */
  source(): SourceRect {
    const width = this.viewport.width / this.scale;
    const height = this.viewport.height / this.scale;

    return { x: this.centerX - width / 2, y: this.centerY - height / 2, width, height };
  }

  /**
   * Trzyma widok w zasięgu mapy powiększonym o {@link OVERSCROLL}.
   *
   * Gdy mapa mieści się w danej osi w całości — a przy najdalszym zoomie zawsze mieści się
   * w jednej z nich — środkujemy zamiast dosuwać do krawędzi. Zapas na wyjazd nie dotyczy
   * tego przypadku: nie ma czego wyprowadzać na środek kadru, bo widać już wszystko,
   * a dryf mapy po ekranie przy pełnym oddaleniu wyglądałby jak błąd.
   */
  private clamp() {
    const { width, height } = this.source();

    this.centerX =
      width >= this.mapWidth
        ? this.mapWidth / 2
        : Camera.between(this.centerX, this.mapWidth, width);

    this.centerY =
      height >= this.mapHeight
        ? this.mapHeight / 2
        : Camera.between(this.centerY, this.mapHeight, height);
  }

  /**
   * Obcina środek widoku do zakresu mapy powiększonego o zapas na wyjazd.
   *
   * Zapas to **połowa kadru**, ograniczona z góry przez {@link OVERSCROLL}. Połowa kadru
   * jest tu jedyną liczbą, która sama skaluje się z przybliżeniem, i wychodzi wprost
   * z tego, po co ten zapas istnieje: pozwala wyprowadzić dowolny punkt mapy — łącznie
   * z jej rogiem — na środek ekranu, i ani piksela dalej. Na skraju zapasu krawędź mapy
   * stoi dokładnie w środku kadru, więc połowa ekranu to wciąż mapa.
   */
  private static between(value: number, mapSize: number, viewSize: number): number {
    const margin = Math.min(mapSize * OVERSCROLL, viewSize / 2);

    return Math.min(mapSize - viewSize / 2 + margin, Math.max(viewSize / 2 - margin, value));
  }
}
