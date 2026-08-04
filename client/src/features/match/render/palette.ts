/**
 * Paleta kafelków: właściciel × teren → gotowy piksel.
 *
 * Policzona **raz** przy `MatchInit`, żeby w pętli rysowania zostało jedno indeksowanie.
 * 256 slotów × 4 typy terenu to tysiąc kilkaset bajtów — tabela mieści się w cache'u
 * procesora, a alternatywa (mnożenie kanałów per kafelek) to dwa miliony mnożeń na klatkę.
 *
 * Tabela ma **dwa piętra**: wypełnienie terytorium i jego otoczka. Renderer wybiera piętro
 * jednym dodawaniem (patrz {@link PALETTE_STRIDE}), więc rysowanie granic nie kosztuje ani
 * jednego rozgałęzienia więcej niż rysowanie środka.
 */
import { Terrain, TERRAIN_TYPE_COUNT } from '../map/tmap';

/** Rezerwacje z D12: `0` to pustkowie, `255` woda, `1..254` sloty aktorów. */
export const WASTELAND_OWNER = 0;
export const WATER_OWNER = 255;

export const OWNER_COUNT = 256;

/**
 * Ile wpisów ma jedno piętro palety — i zarazem przesunięcie do piętra otoczki.
 *
 * `palette[owner * TERRAIN_TYPE_COUNT + terrain]` to wypełnienie, a to samo plus
 * `PALETTE_STRIDE` to otoczka.
 */
export const PALETTE_STRIDE = OWNER_COUNT * TERRAIN_TYPE_COUNT;

/**
 * Ile koloru gracza zostaje na wypełnieniu terytorium; reszta to teren spod spodu.
 *
 * Poprzednia wersja malowała kafelek **czystym** kolorem właściciela i po kilku minutach
 * połowa mapy była płaską plamą: gracz nie widział ani gór, których nie opłaca się
 * atakować, ani przesmyków, którymi warto iść. Przezroczystość oddaje teren z powrotem,
 * a rozpoznawalność koloru bierze na siebie otoczka — pełna, nieprzezroczysta i tylko
 * na granicy, czyli dokładnie tam, gdzie „czyje to jest" naprawdę trzeba wiedzieć.
 */
const TERRITORY_ALPHA = 0.55;

/**
 * O ile rozjaśniany jest kolor gracza na otoczce.
 *
 * Otoczka ma odskoczyć od **własnego wypełnienia**, a nie tylko od sąsiada, i to jest
 * powód, dla którego ta liczba jest tak duża. Wypełnienie na górach jest już jasne — bierze
 * jasność od terenu — więc ciemny kolor gracza obrysowany sobą samym daje granicę, której
 * na przełęczy po prostu nie widać. Jedna trzecia drogi do bieli wystarcza przy każdym
 * kolorze z puli slotów i wciąż zostawia go rozpoznawalnym.
 */
const BORDER_LIGHTEN = 0.35;

/**
 * Kolory terenu niczyjego.
 *
 * Niskonasycone, żeby każdy właściciel od nich odskakiwał — ale **nie ciemne**. Pierwsza
 * wersja tej tablicy miała luminancję rzędu 40 na 255 i mapa czytała się jak zdjęcie nocne;
 * przy kilku procentach zajętego terenu gracz przez większość meczu patrzy właśnie na te
 * cztery kolory, więc to one decydują, czy w ogóle widać, gdzie jest ląd.
 *
 * Jasność rośnie z wysokością — woda najciemniej, góry najjaśniej — bo tak czyta się mapy
 * topograficzne. **Odwrotnie niż przy kafelkach zajętych**, gdzie teren przyciemnia kolor
 * właściciela: tam jaśniejszy odcień znaczy, że gracz ma łatwiejszy teren, a tu chodzi
 * wyłącznie o odczytanie ukształtowania.
 */
const WASTELAND_COLOR: readonly number[] = [0x1c3f63, 0x4e7a43, 0x8a8447, 0x91929a];

/** Woda jest jedna niezależnie od kodu terenu — w `owner[]` i tak trafia jako 255. */
const WATER_COLOR = 0x1c3f63;

/**
 * Czy ta maszyna zapisuje 32-bitowe słowa od najmłodszego bajtu.
 *
 * `ImageData` jest tablicą bajtów w kolejności RGBA, ale wypełnianie jej po 32 bity naraz
 * jest czterokrotnie szybsze. Cena to jedna decyzja o kolejności bajtów — pomylona daje
 * mapę z zamienionymi kanałami czerwonym i niebieskim, czyli obraz, który *wygląda* na
 * działający. Dlatego sprawdzamy zamiast zakładać.
 */
const LITTLE_ENDIAN = (() => {
  const probe = new Uint32Array(1);
  new Uint8Array(probe.buffer)[0] = 1;

  return probe[0] === 1;
})();

function pack(rgb: number): number {
  const red = (rgb >> 16) & 0xff;
  const green = (rgb >> 8) & 0xff;
  const blue = rgb & 0xff;

  return LITTLE_ENDIAN
    ? (0xff << 24) | (blue << 16) | (green << 8) | red
    : (red << 24) | (green << 16) | (blue << 8) | 0xff;
}

/** Miesza dwa kolory RGB; `weight` to udział `over`. Kanały osobno, bez gammy — mapa ma tu
 *  wyglądać jak nałożona folia, a nie jak wynik obliczeń fotometrycznych. */
function mix(base: number, over: number, weight: number): number {
  const channel = (shift: number) => {
    const from = (base >> shift) & 0xff;
    const to = (over >> shift) & 0xff;

    return Math.round(from + (to - from) * weight) & 0xff;
  };

  return (channel(16) << 16) | (channel(8) << 8) | channel(0);
}

export interface SlotColor {
  slot: number;
  colorRgb: number;
}

/**
 * Buduje tablicę `owner * TERRAIN_TYPE_COUNT + terrain → piksel`, w dwóch piętrach.
 *
 * Sloty spoza obsady dostają kolor pustkowia, a nie czerń: gdyby serwer przysłał kiedyś
 * runa ze slotem, którego nie ma w `MatchInit`, mapa ma dalej wyglądać jak mapa. Piętro
 * otoczki dla pustkowia i wody jest kopią wypełnienia — renderer i tak nigdy po nie nie
 * sięgnie, bo obrys ma tylko terytorium, ale tabela ma być kompletna.
 */
export function buildPalette(slots: readonly SlotColor[]): Uint32Array {
  const palette = new Uint32Array(PALETTE_STRIDE * 2);

  const ground = (terrain: number) =>
    terrain === Terrain.water ? WATER_COLOR : WASTELAND_COLOR[terrain];

  for (let owner = 0; owner < OWNER_COUNT; owner++) {
    for (let terrain = 0; terrain < TERRAIN_TYPE_COUNT; terrain++) {
      const entry = owner * TERRAIN_TYPE_COUNT + terrain;

      palette[entry] = pack(ground(terrain));
      palette[PALETTE_STRIDE + entry] = palette[entry];
    }
  }

  // Właściciel „woda" jest wodą niezależnie od kodu terenu: gdyby te dwie tablice kiedyś
  // się rozjechały, ma z tego wyjść morze, a nie łąka pośrodku zatoki.
  for (let terrain = 0; terrain < TERRAIN_TYPE_COUNT; terrain++) {
    const entry = WATER_OWNER * TERRAIN_TYPE_COUNT + terrain;

    palette[entry] = pack(WATER_COLOR);
    palette[PALETTE_STRIDE + entry] = palette[entry];
  }

  for (const { slot, colorRgb } of slots) {
    if (slot === WASTELAND_OWNER || slot === WATER_OWNER) {
      continue;
    }

    for (let terrain = 0; terrain < TERRAIN_TYPE_COUNT; terrain++) {
      const entry = slot * TERRAIN_TYPE_COUNT + terrain;

      // Kafelek wody nie ma właściciela z definicji, więc nawet gdyby run go objął,
      // ma zostać wodą — inaczej terytorium wylewałoby się na morze.
      if (terrain === Terrain.water) {
        palette[entry] = pack(WATER_COLOR);
        palette[PALETTE_STRIDE + entry] = palette[entry];

        continue;
      }

      palette[entry] = pack(mix(WASTELAND_COLOR[terrain], colorRgb, TERRITORY_ALPHA));
      palette[PALETTE_STRIDE + entry] = pack(mix(colorRgb, 0xffffff, BORDER_LIGHTEN));
    }
  }

  return palette;
}
