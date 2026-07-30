/**
 * Klient testowy meczu — wchodzi do gry biletem i wypisuje, co przychodzi.
 *
 * Istnieje po to, żeby dało się sprawdzić serwer gry bez przeglądarki, kanwy i workera.
 * Używa **tego samego codegenu**, co aplikacja (`src/proto/`), więc przy okazji sprawdza
 * schemat od strony TypeScriptu — a to jedyna strona, której testy C++ nie widzą.
 *
 * Wymaga Node 22+ (globalny WebSocket, natywny TypeScript). Zero zależności poza tymi,
 * które klient i tak ma.
 *
 *   npm --prefix client run match -- --url ws://127.0.0.1:5101/match/<matchId> --ticket <jwt>
 */
import { create, fromBinary, toBinary } from '@bufbuild/protobuf';

import { ClientMsgSchema, ServerMsgSchema } from '../src/proto/game_pb.ts';

interface Arguments {
  url: string;
  ticket: string;
}

function parseArguments(argv: readonly string[]): Arguments {
  const values = new Map<string, string>();

  for (let index = 0; index < argv.length; index += 2) {
    values.set(argv[index].replace(/^--/, ''), argv[index + 1] ?? '');
  }

  const url = values.get('url');
  const ticket = values.get('ticket');

  if (!url || !ticket) {
    console.error('Użycie: match-client.ts --url ws://host:port/match/<matchId> --ticket <jwt>');
    process.exit(2);
  }

  return { url, ticket };
}

const { url, ticket } = parseArguments(process.argv.slice(2));

const socket = new WebSocket(url);
socket.binaryType = 'arraybuffer';

let snapshots = 0;
let pingSentAt = 0;

socket.addEventListener('open', () => {
  console.log(`otwarte: ${url}`);

  const hello = create(ClientMsgSchema, { msg: { case: 'hello', value: { ticket } } });

  socket.send(toBinary(ClientMsgSchema, hello));

  // Aplikacyjny ping, nie ramka protokołu: przeglądarka nie daje JavaScriptowi dostępu
  // do natywnych ramek ping/pong, więc RTT trzeba mierzyć własną wiadomością.
  setInterval(() => {
    pingSentAt = Date.now();

    const ping = create(ClientMsgSchema, {
      msg: { case: 'ping', value: { clientTimeMs: BigInt(pingSentAt) } },
    });

    socket.send(toBinary(ClientMsgSchema, ping));
  }, 5000);
});

socket.addEventListener('message', (event: MessageEvent) => {
  const message = fromBinary(ServerMsgSchema, new Uint8Array(event.data as ArrayBuffer));

  switch (message.msg.case) {
    case 'snapshot': {
      snapshots += 1;

      // Pierwszy snapshot jest jedynym dowodem, że bilet przeszedł — potem lecą kolejne.
      if (snapshots <= 3 || snapshots % 25 === 0) {
        const kind = message.msg.value.isKeyframe ? 'keyframe' : 'delta';

        console.log(`snapshot #${snapshots}: tik ${message.msg.value.tick} (${kind})`);
      }

      break;
    }
    case 'init':
      console.log(`MatchInit: mapa ${message.msg.value.mapId}, slot ${message.msg.value.yourSlot}`);

      break;
    case 'pong':
      console.log(`pong: RTT ${Date.now() - pingSentAt} ms`);

      break;
    default:
      console.log(`wiadomość: ${message.msg.case ?? 'pusta'}`);
  }
});

socket.addEventListener('close', (event: CloseEvent) => {
  console.log(`zamknięte: kod ${event.code}${event.reason ? `, powód '${event.reason}'` : ''}`);
  console.log(`odebrano snapshotów: ${snapshots}`);

  process.exit(event.code === 1000 || snapshots > 0 ? 0 : 1);
});

socket.addEventListener('error', () => {
  console.error('błąd połączenia');
});
