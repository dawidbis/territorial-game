/**
 * Renderer mapy — jedyne miejsce w kliencie, które dotyka dwunastu megabajtów.
 *
 * Rysowanie idzie dwustopniowo i to jest cała jego architektura:
 *
 * ```
 * owner[] + terrain[] ──paleta──▶ ImageData 2000×1000 ──putImageData──▶ kanwa mapy
 *                                                                            │ drawImage z kamerą
 *                                                                            ▼
 *                                                                      kanwa widoczna
 * ```
 *
 * Bitmapa mapy jest przepisywana **tylko wtedy, gdy zmieni się stan** — przesuwanie
 * i zoom to sam `drawImage`, czyli operacja, którą robi GPU. Rysowanie kafelek po kafelku
 * wprost na widoczną kanwę przy każdej klatce dawałoby dwa miliony operacji sześćdziesiąt
 * razy na sekundę i nie było wykonalne.
 */
import { TERRAIN_TYPE_COUNT } from '../map/tmap';
import { Camera, SourceRect } from './camera';
import { PALETTE_STRIDE, WASTELAND_OWNER, WATER_OWNER } from './palette';

/** Kolor tego, co jest poza mapą — kamerze wolno tam zajrzeć. */
const VOID_COLOR = '#080d16';

/**
 * Czy kafelek leży na granicy terytorium, czyli czy dostaje obrys.
 *
 * Granicą jest każdy własny kafelek stykający się z czymkolwiek innym: cudzym terytorium,
 * pustkowiem, wodą albo krawędzią mapy. **Krawędź mapy liczy się jako obcy sąsiad** — bez
 * tego państwo dochodzące do brzegu świata byłoby obrysowane w trzech czwartych i wyglądałoby
 * na niedomalowane.
 *
 * Osobno od klasy i bez odwołania do kanwy, bo cała trudność obrysu siedzi w tych czterech
 * porównaniach i w warunkach brzegowych — a te da się sprawdzić bez przeglądarki.
 */
export function isBorderTile(
  owner: Uint8Array,
  width: number,
  height: number,
  x: number,
  y: number,
): boolean {
  const index = y * width + x;
  const slot = owner[index];

  if (slot === WASTELAND_OWNER || slot === WATER_OWNER) {
    return false;
  }

  return (
    x === 0 ||
    owner[index - 1] !== slot ||
    x === width - 1 ||
    owner[index + 1] !== slot ||
    y === 0 ||
    owner[index - width] !== slot ||
    y === height - 1 ||
    owner[index + width] !== slot
  );
}

/** Jak długo gaśnie błysk po przejęciu kafelka. */
const CAPTURE_FLASH_MS = 260;

/**
 * Siła błysku w chwili przejęcia: 0 to brak, 1 to biel.
 *
 * Własne przejęcia świecą mocniej i to jest cała różnica między „coś się rusza na mapie"
 * a „to ja właśnie to wziąłem". Przy stu graczach front rusza się wszędzie naraz, więc bez
 * tego rozróżnienia własne natarcie ginie w cudzym ruchu.
 */
const OWN_FLASH = 0.85;
const OTHER_FLASH = 0.45;

/**
 * Podpis terytorium: nick gracza i jego żywa populacja, rysowane wprost na mapie.
 *
 * Pozycja jest w kafelkach, a nie w pikselach — kamera przelicza ją na ekran przy każdej
 * klatce, więc podpis trzyma się terytorium przy przesuwaniu i zoomie.
 */
export interface MapLabel {
  /** Kafelek, nad którym siada podpis. */
  x: number;
  y: number;
  name: string;
  /** Populacja **już sformatowana** — renderer nie zna przelicznika jednostek. */
  people: string;
  /** Ile kafelków ma to terytorium; z tego dobierana jest wielkość pisma. */
  tiles: number;
}

/**
 * Wielkość pisma podpisu jako ułamek boku kwadratu o polu równym terytorium.
 *
 * Podpis ma rosnąć razem z państwem, a nie z zoomem: inaczej przy oddaleniu wszystkie nicki
 * zlewają się w jedną plamę, a przy przybliżeniu jeden nick zajmuje pół ekranu.
 */
const LABEL_SCALE = 0.26;

/** Poniżej tylu pikseli CSS podpis jest nieczytelny i tylko zaśmieca mapę — nie rysujemy go. */
const LABEL_MIN_PX = 9;

/** Sufit, żeby państwo wielkości kontynentu nie wypisywało nicku przez cały ekran. */
const LABEL_MAX_PX = 44;

/** Jedna paczka kafelków, które przyszły razem i razem gasną. */
interface Flash {
  tiles: Uint32Array;

  /** Siła błysku w chwili zero — patrz {@link OWN_FLASH}. */
  strength: number;

  started: number;

  // Prostokąt policzony raz przy rejestracji: kafelki się nie ruszają, a `putImageData`
  // i tak przyjmuje obszar, nie listę.
  minX: number;
  minY: number;
  maxX: number;
  maxY: number;
}

/**
 * Rozjaśnia gotowy piksel w stronę bieli.
 *
 * Miesza **każdy z czterech bajtów osobno** i dlatego nie musi wiedzieć, w jakiej kolejności
 * leżą kanały: kanał alfa jest już równy 255, a 255 zmieszane z bielą to nadal 255. Wersja
 * rozpakowująca kanały po nazwie musiałaby powtórzyć decyzję o kolejności bajtów z palety,
 * czyli mieć drugie miejsce, w którym da się ją pomylić — a pomylona daje obraz, który
 * *wygląda* na działający.
 */
export function lighten(color: number, strength: number): number {
  const mix = (value: number) => Math.round(value + (0xff - value) * strength);

  return (
    ((mix((color >>> 24) & 0xff) << 24) |
      (mix((color >>> 16) & 0xff) << 16) |
      (mix((color >>> 8) & 0xff) << 8) |
      mix(color & 0xff)) >>>
    0
  );
}

export class MapRenderer {
  private readonly map: OffscreenCanvas;
  private readonly mapContext: OffscreenCanvasRenderingContext2D;
  private readonly image: ImageData;

  /** Widok 32-bitowy na te same bajty co `image.data` — jeden zapis zamiast czterech. */
  private readonly pixels: Uint32Array;

  private readonly view: OffscreenCanvasRenderingContext2D;

  /** Trwające błyski. Pusta lista znaczy „nie ma czego animować" i kosztuje jedno `if`. */
  private flashes: Flash[] = [];

  /**
   * Tablica właścicieli, z której liczony jest kolor docelowy.
   *
   * Trzymana tutaj, a nie w paczce błysku, bo kafelek potrafi zmienić właściciela w trakcie
   * gaśnięcia. Paczka zapisująca kolor zapamiętany przy rejestracji przemalowałaby go
   * z powrotem na poprzedniego zdobywcę — czyli cofnęła stan mapy.
   */
  private owner: Uint8Array | null = null;

  /** Slot tego gracza; `null`, dopóki nie przyjdzie `MatchInit`. */
  private viewer: number | null = null;

  /** Podpisy terytoriów. Przeliczane co sekundę, rysowane co klatkę — patrz {@link MapLabel}. */
  private labels: readonly MapLabel[] = [];

  /**
   * Pikseli urządzenia na piksel CSS.
   *
   * Kanwa liczy w pikselach urządzenia, a czytelność pisma — w CSS-owych. Bez tego progi
   * wielkości podpisu znaczyłyby co innego na ekranie zwykłym i co innego na gęstym.
   */
  private ratio = 1;

  constructor(
    private readonly canvas: OffscreenCanvas,
    private readonly width: number,
    private readonly height: number,
    private readonly terrain: Uint8Array,
    private palette: Uint32Array,
  ) {
    this.map = new OffscreenCanvas(width, height);

    const mapContext = this.map.getContext('2d', { alpha: false });
    const viewContext = canvas.getContext('2d', { alpha: false });

    if (!mapContext || !viewContext) {
      throw new Error('Nie udało się utworzyć kontekstu 2D na kanwie mapy.');
    }

    this.mapContext = mapContext;
    this.view = viewContext;

    this.image = new ImageData(width, height);
    this.pixels = new Uint32Array(this.image.data.buffer);

    // Bez tego zoom daje rozmytą papkę zamiast pikseli — a kafelek MA być kwadratem.
    this.view.imageSmoothingEnabled = false;
  }

  setPalette(palette: Uint32Array) {
    this.palette = palette;
  }

  setViewerSlot(slot: number) {
    this.viewer = slot;
  }

  setLabels(labels: readonly MapLabel[]) {
    this.labels = labels;
  }

  setPixelRatio(ratio: number) {
    this.ratio = Math.max(1, ratio);
  }

  /** Przepisuje całą mapę z tablicy właścicieli. Wołane po keyframie. */
  paintAll(owner: Uint8Array) {
    const { pixels, width, height } = this;

    this.owner = owner;

    // Keyframe to stan pełny, a nie zmiana: błyski z poprzedniego stanu opisywałyby
    // przejęcia, których ten obraz już nie pamięta. Po reconnekcie mapa ma po prostu być.
    this.flashes = [];

    // Pętla po wierszach i kolumnach, a nie po samym indeksie: obrys pyta o sąsiadów, więc
    // potrzebuje `x` i `y`, a dzielenie z resztą dwa miliony razy kosztuje więcej niż licznik.
    let index = 0;

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++, index++) {
        pixels[index] = this.colorAt(index, x, y, owner);
      }
    }

    this.mapContext.putImageData(this.image, 0, 0);
  }

  /**
   * Kolor kafelka: wypełnienie albo otoczka.
   *
   * To jest cały algorytm rysowania granic — nie ma osobnej warstwy, nie ma ścieżek, jest
   * jeden piksel wzięty z drugiego piętra palety.
   */
  private colorAt(index: number, x: number, y: number, owner: Uint8Array): number {
    const entry = owner[index] * TERRAIN_TYPE_COUNT + this.terrain[index];

    return isBorderTile(owner, this.width, this.height, x, y)
      ? this.palette[PALETTE_STRIDE + entry]
      : this.palette[entry];
  }

  /**
   * Przyjmuje kafelki, które zmieniły właściciela, i **zaczyna ich błysk**.
   *
   * Kolor docelowy nie ląduje tu od razu: paczka trafia na listę gasnących i pierwszą klatkę
   * dostaje rozjaśnioną. Bez tego front przeskakiwał skokowo co paczkę snapshotu i jedyną
   * informacją o kierunku natarcia było porównanie dwóch nieruchomych obrazów.
   *
   * `putImageData` obejmuje cały prostokąt obejmujący zmiany, bo sto wywołań na pojedyncze
   * piksele kosztuje więcej niż jedno na obszar — a ekspansja jest przestrzennie ciągła,
   * więc ten prostokąt zwykle jest mały. **Prostokąt jest per paczka, nie globalny**: dwa
   * natarcia na przeciwnych końcach mapy dają dwa małe obszary zamiast jednego wielkiego,
   * a przy mapie 2000×1000 różnica to megabajty na klatkę.
   */
  paintTiles(indices: ArrayLike<number>, owner: Uint8Array) {
    if (indices.length === 0) {
      return;
    }

    const { width } = this;

    this.owner = owner;

    let minX = width;
    let minY = this.height;
    let maxX = 0;
    let maxY = 0;

    const tiles = new Uint32Array(indices.length);

    // Własną paczkę poznajemy po pierwszym kafelku: delty przychodzą pogrupowane po
    // właścicielu, ale jedna paczka snapshotu potrafi objąć kilku zdobywców. Rozdzielanie
    // jej na paczkę per gracz kosztowałoby tyle prostokątów, ilu graczy w oknie — a różnica
    // dotyczy wyłącznie tego, jak mocno błyśnie kafelek sąsiada.
    let own = false;

    for (let i = 0; i < indices.length; i++) {
      const index = indices[i];

      tiles[i] = index;

      if (this.viewer !== null && owner[index] === this.viewer) {
        own = true;
      }

      const x = index % width;
      const y = (index - x) / width;

      minX = Math.min(minX, x);
      maxX = Math.max(maxX, x);
      minY = Math.min(minY, y);
      maxY = Math.max(maxY, y);
    }

    const flash: Flash = {
      tiles,
      strength: own ? OWN_FLASH : OTHER_FLASH,
      started: this.clockNow(),
      minX,
      minY,
      maxX,
      maxY,
    };

    this.flashes.push(flash);

    // Sąsiedzi PRZED paczką: przejęcie zmienia obrys po obu stronach granicy, a kafelek
    // z paczki zaraz i tak dostanie swój kolor z błyskiem. Sąsiad, który akurat sam gaśnie,
    // traci resztkę błysku — po dwóch snapshotach zostało z niej kilka procent, więc na
    // ekranie tego nie widać, a wariant z zapamiętaną siłą błysku per kafelek kosztowałby
    // megabajty na coś, czego nikt nie zobaczy.
    this.repaintNeighbors(tiles, owner);
    this.repaint(flash, flash.strength);
  }

  /** Przelicza obrys kafelków dookoła paczki. Bez błysku — one nie zmieniły właściciela. */
  private repaintNeighbors(tiles: Uint32Array, owner: Uint8Array) {
    const { width, height, pixels } = this;

    for (let i = 0; i < tiles.length; i++) {
      const index = tiles[i];
      const x = index % width;
      const y = (index - x) / width;

      if (x > 0) {
        pixels[index - 1] = this.colorAt(index - 1, x - 1, y, owner);
      }

      if (x < width - 1) {
        pixels[index + 1] = this.colorAt(index + 1, x + 1, y, owner);
      }

      if (y > 0) {
        pixels[index - width] = this.colorAt(index - width, x, y - 1, owner);
      }

      if (y < height - 1) {
        pixels[index + width] = this.colorAt(index + width, x, y + 1, owner);
      }
    }
  }

  /**
   * Przesuwa wszystkie błyski do czasu `now`.
   *
   * @returns `true`, dopóki cokolwiek się jeszcze pali — pętla klatek trzyma na tym
   * decyzję, czy w ogóle rysować. Bez tego animacja stawałaby na nieruchomej kamerze,
   * bo tam rysowanie jest warunkowane zmianą stanu.
   */
  animate(now: number): boolean {
    if (this.flashes.length === 0) {
      return false;
    }

    const remaining: Flash[] = [];

    for (const flash of this.flashes) {
      const progress = (now - flash.started) / CAPTURE_FLASH_MS;

      if (progress >= 1) {
        // Ostatnia klatka to czysty kolor docelowy, a nie „prawie": bez tego na mapie
        // zostawałby ślad rozjaśnienia na zawsze.
        this.repaint(flash, 0);

        continue;
      }

      // Ease-out: błysk gaśnie szybko na starcie i długo dochodzi do zera. Liniowy czyta
      // się jak przenikanie slajdów, a nie jak uderzenie.
      const eased = 1 - progress;

      this.repaint(flash, flash.strength * eased * eased);

      remaining.push(flash);
    }

    this.flashes = remaining;

    return this.flashes.length > 0;
  }

  /** Maluje jedną paczkę z zadaną siłą rozjaśnienia i odsyła jej prostokąt na kanwę mapy. */
  private repaint(flash: Flash, strength: number) {
    const { pixels, width, owner } = this;

    if (!owner) {
      return;
    }

    for (let i = 0; i < flash.tiles.length; i++) {
      const index = flash.tiles[i];
      const x = index % width;
      const target = this.colorAt(index, x, (index - x) / width, owner);

      pixels[index] = strength > 0 ? lighten(target, strength) : target;
    }

    // Prostokąt rozszerzony o jeden kafelek na każdą stronę: przemalowani sąsiedzi leżą
    // dokładnie tam i bez tego zostaliby na kanwie mapy w starym kolorze.
    const left = Math.max(0, flash.minX - 1);
    const top = Math.max(0, flash.minY - 1);
    const right = Math.min(width - 1, flash.maxX + 1);
    const bottom = Math.min(this.height - 1, flash.maxY + 1);

    this.mapContext.putImageData(this.image, 0, 0, left, top, right - left + 1, bottom - top + 1);
  }

  /**
   * Zegar animacji.
   *
   * `performance.now()` przez metodę, żeby test mógł podstawić własny czas — animacja bez
   * kontroli nad zegarem jest testowalna wyłącznie przez czekanie, a testy, które czekają,
   * migoczą.
   */
  protected clockNow(): number {
    return performance.now();
  }

  resize(width: number, height: number) {
    this.canvas.width = width;
    this.canvas.height = height;

    // Kontekst gubi ustawienia przy każdej zmianie rozmiaru kanwy — łącznie z tym jednym,
    // bez którego zoom robi się rozmyty.
    this.view.imageSmoothingEnabled = false;
  }

  draw(camera: Camera) {
    const source = camera.source();

    // Tło zamiast czyszczenia: kamerze wolno wyjechać poza mapę, więc obok niej jest teraz
    // kawałek pustki. Czarna wyglądałaby jak niedomalowana klatka — ta czyta się jak brzeg
    // świata, bo jest wyraźnie ciemniejsza od wody i jednolita.
    this.view.fillStyle = VOID_COLOR;
    this.view.fillRect(0, 0, this.canvas.width, this.canvas.height);

    this.view.drawImage(
      this.map,
      source.x,
      source.y,
      source.width,
      source.height,
      0,
      0,
      this.canvas.width,
      this.canvas.height,
    );

    this.drawLabels(camera, source);
  }

  /**
   * Wypisuje nicki i populacje na terytoriach.
   *
   * Rysowane **na widocznej kanwie**, a nie w bitmapę mapy: pismo ma mieć stałą wielkość
   * względem ekranu i ostre krawędzie, a wpisane w bitmapę skalowałoby się razem z nią
   * i przy oddaleniu zamieniło w kilka rozmytych pikseli.
   */
  private drawLabels(camera: Camera, source: SourceRect) {
    if (this.labels.length === 0) {
      return;
    }

    const scale = camera.tileSize;
    const context = this.view;

    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.lineJoin = 'round';
    context.strokeStyle = 'rgba(0, 0, 0, 0.75)';

    for (const label of this.labels) {
      // Bok kwadratu o polu równym terytorium — miara „jak duże jest to państwo na ekranie",
      // która nie zależy od jego kształtu.
      const size = Math.min(
        LABEL_MAX_PX * this.ratio,
        Math.sqrt(label.tiles) * scale * LABEL_SCALE,
      );

      if (size < LABEL_MIN_PX * this.ratio) {
        continue;
      }

      const x = (label.x - source.x) * scale;
      const y = (label.y - source.y) * scale;

      // Zapas na szerokość napisu, żeby podpis nie znikał w chwili, gdy jego środek wyjdzie
      // za kadr — nick bywa dłuższy niż wysoki i wtedy urywałby się w połowie.
      if (
        x < -size * 8 ||
        y < -size * 2 ||
        x > this.canvas.width + size * 8 ||
        y > this.canvas.height + size * 2
      ) {
        continue;
      }

      context.lineWidth = Math.max(1, size / 6);

      context.font = `600 ${size}px system-ui, sans-serif`;
      context.fillStyle = '#ffffff';
      context.strokeText(label.name, x, y - size * 0.55);
      context.fillText(label.name, x, y - size * 0.55);

      // Populacja mniejsza i przygaszona: podpisem jest nick, liczba jest przypisem do niego.
      const smaller = size * 0.78;

      context.font = `500 ${smaller}px system-ui, sans-serif`;
      context.lineWidth = Math.max(1, smaller / 6);
      context.fillStyle = 'rgba(255, 255, 255, 0.85)';
      context.strokeText(label.people, x, y + size * 0.6);
      context.fillText(label.people, x, y + size * 0.6);
    }
  }
}
