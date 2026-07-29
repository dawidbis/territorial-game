/** Formatuje sekundy jako mm:ss. Zwraca `--:--`, gdy nie ma czego pokazać. */
export function formatCountdown(seconds: number | null): string {
  if (seconds === null || seconds < 0) {
    return '--:--';
  }

  const minutes = Math.floor(seconds / 60);
  const rest = seconds % 60;

  return `${String(minutes).padStart(2, '0')}:${String(rest).padStart(2, '0')}`;
}
