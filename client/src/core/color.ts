import { HsvColor } from '../types/player';

/**
 * CSS nie zna hsv(), a API operuje na HSV, więc przeliczamy na HSL.
 * Bez tego HSV(0, 100, 100) — czysta czerwień — wyszłoby białe.
 */
export function hsvToCss({ hue, saturation, value }: HsvColor): string {
  const brightness = value / 100;
  const lightness = brightness * (1 - saturation / 200);
  const edge = Math.min(lightness, 1 - lightness);
  const hslSaturation = edge === 0 ? 0 : (brightness - lightness) / edge;

  return `hsl(${hue} ${hslSaturation * 100}% ${lightness * 100}%)`;
}
