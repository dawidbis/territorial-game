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
  /** Slot do zaatakowania po wejściu; `0` to pustkowie, brak wartości — nie atakuj. */
  attack?: number;
  /** Ile procent puli wysłać w tym ataku. */
  percent: number;
}

function parseArguments(argv: readonly string[]): Arguments {
  const values = new Map<string, string>();

  for (let index = 0; index < argv.length; index += 2) {
    values.set(argv[index].replace(/^--/, ''), argv[index + 1] ?? '');
  }

  const url = values.get('url');
  const ticket = values.get('ticket');
  const attack = values.get('attack');

  if (!url || !ticket) {
    console.error(
      'Użycie: match-client.ts --url ws://host:port/ws/match/<matchId> --ticket <jwt> ' +
        '[--attack <slot|0>] [--percent <1-100>]',
    );
    process.exit(2);
  }

  return {
    url,
    ticket,
    attack: attack === undefined ? undefined : Number(attack),
    percent: Number(values.get('percent') ?? 50),
  };
}

const { url, ticket, attack, percent } = parseArguments(process.argv.slice(2));

const socket = new WebSocket(url);
socket.binaryType = 'arraybuffer';

let snapshots = 0;
let pingSentAt = 0;

/// Ile kafelków zmieniło właściciela od początku połączenia.
let conquered = 0;

/** Liczba kafelków mapy z `MatchInit` — potrzebna, żeby ocenić pokrycie keyframe'a. */
let tiles = 0;

function toHex(bytes: Uint8Array): string {
  return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}

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
  const payload = new Uint8Array(event.data as ArrayBuffer);
  const message = fromBinary(ServerMsgSchema, payload);

  switch (message.msg.case) {
    case 'snapshot': {
      snapshots += 1;

      const snapshot = message.msg.value;

      // Keyframe rozpisujemy szczegółowo, bo to on jest mapą. Zliczenie kafelków objętych
      // runami jest jedynym sprawdzeniem, które łapie błąd o jeden w `startDelta` — a taki
      // błąd wygląda jak przesunięty kontynent, nie jak awaria.
      if (snapshot.isKeyframe) {
        const covered = snapshot.runs.reduce((sum, run) => sum + run.length, 0);
        const slots = new Set(snapshot.runs.map((run) => run.slot));

        let cursor = 0;
        let ordered = true;

        for (const run of snapshot.runs) {
          cursor += run.startDelta + run.length;

          if (cursor > tiles) {
            ordered = false;
          }
        }

        console.log(
          `keyframe: ${snapshot.runs.length} runów, ${covered} kafelków ` +
            `(${tiles ? ((100 * covered) / tiles).toFixed(1) : '?'}% mapy), ` +
            `${slots.size} różnych właścicieli, ${payload.byteLength} B`,
        );

        console.log(`keyframe mieści się w mapie: ${ordered ? 'tak' : 'NIE'}`);

        // Rozkaz wychodzi dopiero po keyframie: wcześniej nie wiadomo, czy w ogóle stoimy
        // na mapie, a atak z pustego terytorium nie miałby z czego wyjść.
        if (attack !== undefined) {
          const command = create(ClientMsgSchema, {
            msg: {
              case: 'command',
              value: {
                seq: 1,
                order: { case: 'attack', value: { targetSlot: attack, populationPct: percent } },
              },
            },
          });

          socket.send(toBinary(ClientMsgSchema, command));

          console.log(`rozkaz: atak na slot ${attack} siłą ${percent}% puli`);
        }

        break;
      }

      // Delty niosą kafelki, które zmieniły właściciela. Sumujemy je, bo to jedyny dowód
      // z tej strony, że symulacja rusza mapą — a nie tylko tyka licznikiem.
      if (snapshot.deltas.length > 0) {
        const moved = snapshot.deltas.reduce((sum, group) => sum + group.indexDeltas.length, 0);

        conquered += moved;

        if (snapshots % 25 === 0) {
          console.log(
            `snapshot #${snapshots}: tik ${snapshot.tick}, ${moved} kafelków zmieniło ` +
              `właściciela (łącznie ${conquered})`,
          );
        }

        break;
      }

      if (snapshot.others.length > 0) {
        const total = snapshot.others.reduce((sum, state) => sum + state.territoryTiles, 0);

        console.log(
          `snapshot #${snapshots}: tik ${snapshot.tick}, ranking ${snapshot.others.length} ` +
            `aktorów, ${total} kafelków w terytoriach`,
        );

        break;
      }

      if (snapshots <= 3 || snapshots % 25 === 0) {
        console.log(`snapshot #${snapshots}: tik ${snapshot.tick} (delta)`);
      }

      break;
    }
    case 'myState': {
      const state = message.msg.value;

      // Rzadziej niż co snapshot: `MyState` leci 5 Hz, a w konsoli chodzi o to, żeby było
      // widać kierunek — pula rośnie, złoto rośnie, armia w polu topnieje.
      if (snapshots % 25 === 0) {
        console.log(
          `stan: ${state.population}/${state.maxPopulation} ludzi (+${state.popIncome}/s), ` +
            `${state.gold} złota (+${state.goldIncome}/s, podatek +${state.taxAmount} ` +
            `za ${Math.round(state.taxInMs / 1000)} s), ` +
            `${state.attackForce} w polu, ${state.cities} miast`,
        );
      }

      break;
    }
    case 'rejected': {
      const rejected = message.msg.value;

      console.log(`rozkaz #${rejected.seq} odrzucony: ${rejected.reason}`);

      break;
    }
    case 'init': {
      const init = message.msg.value;

      tiles = init.mapWidth * init.mapHeight;

      console.log(
        `MatchInit: mapa '${init.mapId}' ${init.mapWidth}×${init.mapHeight}, ` +
          `slot ${init.yourSlot}, ${init.tickRate} Hz, ziarno ${init.seed}`,
      );

      console.log(`teren sha256: ${toHex(init.mapSha256)}`);

      const bots = init.slots.filter((slot) => slot.isBot).length;
      const own = init.slots.find((slot) => slot.slot === init.yourSlot);

      console.log(
        `obsada: ${init.slots.length} aktorów (${init.slots.length - bots} ludzi, ${bots} botów)` +
          (own ? `; ja to '${own.name}' w #${own.colorRgb.toString(16).padStart(6, '0')}` : ''),
      );

      break;
    }
    case 'pong':
      console.log(`pong: RTT ${Date.now() - pingSentAt} ms`);

      break;
    default:
      console.log(`wiadomość: ${message.msg.case ?? 'pusta'}`);
  }
});

socket.addEventListener('close', (event: CloseEvent) => {
  console.log(`zamknięte: kod ${event.code}${event.reason ? `, powód '${event.reason}'` : ''}`);
  console.log(`odebrano snapshotów: ${snapshots}, przejętych kafelków: ${conquered}`);

  process.exit(event.code === 1000 || snapshots > 0 ? 0 : 1);
});

socket.addEventListener('error', () => {
  console.error('błąd połączenia');
});
