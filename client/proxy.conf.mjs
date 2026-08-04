/**
 * Proxy dev-servera.
 *
 * Klient chodzi po https, a game-serwer mówi gołym ws — przeglądarka nie otworzy `ws://`
 * ze strony podanej po https i blokada jest twarda. Rozwiązaniem jest to samo, czym jest
 * produkcja: proxy terminujące TLS przed procesem meczu (plan serwera gry, §3.11).
 *
 * `.mjs`, a nie `.json`, z dwóch powodów. Wpisów jest tyle, ile portów w puli meta, więc
 * lista musi się brać z jednego źródła — przepisana ręcznie rozjechałaby się przy pierwszej
 * zmianie rozmiaru puli i objawiła dopiero jako mecz, do którego nie da się wejść. Drugi
 * powód to `rewrite`: funkcji nie da się zapisać w JSON-ie.
 */
import { readFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));

/** Ustawienia dev meta — jedyne miejsce, w którym pula portów jest zdefiniowana. */
const settingsPath = resolve(here, '../meta/src/Territorial.Meta.Api/appsettings.Development.json');

const settings = JSON.parse(readFileSync(settingsPath, 'utf8')).Match ?? {};

const firstPort = settings.GameServerPort;
const portCount = settings.GameServerPortCount;

// Twardy błąd zamiast domyślnej wartości: zgadnięta pula znaczy proxy routujące do portów,
// na których nikt nie stoi, a to widać dopiero jako mecz bez połączenia.
if (!Number.isInteger(firstPort) || !Number.isInteger(portCount) || portCount < 1) {
  throw new Error(
    `Match:GameServerPort i Match:GameServerPortCount muszą być liczbami w ${settingsPath} — ` +
      'to z nich bierze się lista wpisów proxy dla procesów meczów.',
  );
}

const proxy = {
  '/maps': {
    // Teren spod tego samego origin co reszta. Na produkcji stoi tam CDN i klient nie
    // zauważa różnicy.
    target: 'https://localhost:5001',
    secure: false,
  },
};

// Jeden wpis na port puli. Dev-server nie umie wybierać celu per żądanie, więc o tym, do
// którego procesu idzie połączenie, decyduje segment `gsN` w ścieżce — numer wpisu w puli,
// nie port (D9: klient nie ogląda adresów sieci wewnętrznej). Adres w tym kształcie buduje
// alokator meta, bo tylko on wie, że w dev przed procesami stoi właśnie to proxy.
for (let slot = 0; slot < portCount; slot++) {
  const prefix = `/ws/match/gs${slot}/`;

  proxy[prefix] = {
    // Prefiks /ws/ jest konieczny, nie kosmetyczny: klient ma własną trasę /match/{id}
    // i przy tej samej ścieżce odświeżenie strony w trakcie meczu wysyłało żądanie
    // dokumentu HTML na WebSocket.
    target: `ws://127.0.0.1:${firstPort + slot}`,
    ws: true,
    // Proces meczu zna wyłącznie /ws/match/{matchId} i wszystko inne odrzuca, więc segment
    // adresujący proxy musi tu zniknąć.
    rewrite: (url) => url.replace(prefix, '/ws/match/'),
  };
}

export default proxy;
