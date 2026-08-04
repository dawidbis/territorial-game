/**
 * Liczby w interfejsie meczu.
 *
 * Surowe wartości z serwera są nieczytelne w rozgrywce: „992657" trzeba przeczytać cyfra po
 * cyfrze, żeby wiedzieć, czy to dużo. Gracz porównuje rzędy wielkości, nie jednostki.
 */

/** Ile ludzi z symulacji przypada na jednego „człowieka" pokazywanego graczowi. */
const POPULATION_PER_UNIT = 10;

/**
 * Skraca liczbę do notacji k/m.
 *
 * Jedno miejsce po przecinku **zawsze**, także gdy wypada zero: „1.0k" i „1.4k" mają tę samą
 * szerokość, więc licznik przy suficie nie drga w takt przyrostu. Poniżej tysiąca notacja nie
 * ma czego skracać.
 */
export function formatCompact(value: number): string {
  const rounded = Math.max(0, Math.round(value));

  if (rounded < 1_000) {
    return String(rounded);
  }

  if (rounded < 1_000_000) {
    return `${(rounded / 1_000).toFixed(1)}k`;
  }

  return `${(rounded / 1_000_000).toFixed(1)}m`;
}

/**
 * Liczba ludzi tak, jak widzi ją gracz.
 *
 * Symulacja liczy w jednostkach dziesięć razy drobniejszych niż interfejs — dzięki temu
 * przyrost na tik jest liczbą całkowitą o sensownej rozdzielczości, a gracz nie ogląda
 * wartości z zerem na końcu. Dzielenie jest **wyłącznie prezentacją**: rozkazy i wzory
 * chodzą na wartościach z serwera.
 */
export function formatPopulation(value: number): string {
  return formatCompact(value / POPULATION_PER_UNIT);
}
