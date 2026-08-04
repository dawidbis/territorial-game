/**
 * Kontrakt między wątkiem głównym a workerem meczu.
 *
 * Wszystko, co tędy przechodzi, ma **kilkaset bajtów**. To nie jest oszczędność, tylko cały
 * sens podziału: 12 MB tablic typowanych (teren, właściciele, piksele) żyje wyłącznie
 * w workerze, a Angular dostaje tyle, ile potrzebuje pasek stanu. Kanwa jedzie w drugą
 * stronę raz, jako `Transferable`.
 */

/** Slot obsadzony w meczu — tyle o graczu potrzebuje UI. */
export interface SlotView {
  slot: number;
  name: string;
  colorRgb: number;
  isBot: boolean;
}

export interface StandingView {
  slot: number;
  tiles: number;
}

/** Stan własnego państwa — jedyna wiadomość serwera, której treść zależy od odbiorcy. */
export interface OwnState {
  population: number;
  maxPopulation: number;
  /** Przyrost ludzi **na sekundę**, nie na tik. */
  popIncome: number;
  gold: number;
  goldIncome: number;
  /** Ludzie związani w trwających natarciach. */
  attackForce: number;
  cities: number;
  /** Cena kolejnego miasta — liczy ją serwer, żeby przycisk nie obiecywał innej. */
  nextCityCost: number;
  /** Złoto, które przyniesie najbliższy pobór podatku przy dzisiejszej puli ludzi. */
  taxAmount: number;
  /** Ile milisekund do poboru; z tego i z okresu rysuje się pasek nad złotem. */
  taxInMs: number;
  /** Długość pełnego cyklu podatkowego w milisekundach. */
  taxPeriodMs: number;
}

export type ToWorker =
  | {
      type: 'start';
      canvas: OffscreenCanvas;
      url: string;
      ticket: string;
      viewport: Viewport;
      ratio: number;
    }
  | { type: 'ticket'; ticket: string }
  | { type: 'viewport'; viewport: Viewport; ratio: number }
  | { type: 'pan'; dx: number; dy: number }
  /**
   * Kierunek przesuwania z klawiatury: po jednym znaku na oś, `0` to „nic wciśnięte".
   *
   * Wątek główny przysyła **stan klawiszy**, a nie kroki przesunięcia — inaczej płynność
   * ruchu zależałaby od tego, jak szybko system powtarza wciśnięty klawisz. Sam ruch liczy
   * worker przy każdej klatce, z czasu, jaki od niej minął.
   */
  | { type: 'pan-keys'; dx: number; dy: number }
  | { type: 'zoom'; factor: number; x: number; y: number }
  /** Kadr na terytorium wskazanego slotu — całe, wyśrodkowane. */
  | { type: 'focus'; slot: number }
  /**
   * Kliknięcie w mapę: atak na właściciela wskazanego kafelka.
   *
   * Cel wyznacza **worker**, a nie wątek główny — to on ma tablicę właścicieli i kamerę,
   * czyli obie rzeczy potrzebne, żeby z punktu ekranu zrobić numer slotu.
   */
  | { type: 'attack-at'; x: number; y: number; percent: number }
  | { type: 'build-city' }
  /** Klatka z `requestAnimationFrame` wątku głównego — worker nie ma do niego dostępu. */
  | { type: 'frame' }
  | { type: 'stop' };

export interface Viewport {
  width: number;
  height: number;
}

/** Stan połączenia pokazywany graczowi jednym słowem. */
export type LinkState = 'connecting' | 'live' | 'reconnecting' | 'closed';

export type FromWorker =
  | { type: 'link'; state: LinkState; detail?: string }
  | {
      type: 'init';
      mapId: string;
      width: number;
      height: number;
      yourSlot: number;
      slots: SlotView[];
      tickRate: number;
    }
  | { type: 'terrain'; state: 'loading' | 'ready' | 'error'; detail?: string }
  | { type: 'map'; runs: number; tiles: number; bytes: number }
  | { type: 'tick'; tick: number }
  | { type: 'rtt'; milliseconds: number }
  | { type: 'standings'; entries: StandingView[] }
  | { type: 'own-state'; state: OwnState }
  /** Serwer odrzucił rozkaz. Rozkaz, który znika bez śladu, wygląda jak zerwana sieć. */
  | { type: 'order-rejected'; detail: string }
  /** Bilet przepadł albo wygasł — wątek główny ma jedyny dostęp do HTTP i do `MatchGateway`. */
  | { type: 'need-ticket' }
  | { type: 'fatal'; detail: string };
