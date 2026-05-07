# Binance Service Plugin

This plugin prepares and parses public Binance Spot kline WebSocket data for
BotManager. It also contains dry-run guarded signed-endpoint helper functions
for later trading integration.

## Safety boundaries

- `plugin.binance.dry_run=1` by default: signed order/account helpers log and
  skip network requests unless an operator explicitly disables dry run.
- Live signed helpers require both `BINANCE_API_KEY` and `BINANCE_API_SECRET`.
- Signed helpers reject overlong credentials, invalid symbols, and unsupported
  client order id characters before building requests.
- Order cancel uses Binance `DELETE /api/v3/order`, not the order-create POST.
- The signed REST base follows `plugin.binance.rest_base`, so operators can
  point dry-run or test deployments at a non-production base before enabling
  anything live.
- No live-trading router integration is implemented.
- No socket loop is started by the current scaffold.

The active admin command surface remains public market-data only. It covers
kline stream names, combined-stream wrapper type and symbol-consistency
validation, fail-closed root/data/k object, event/open/close integer-timestamp,
symbol/string-field, finite OHLCV, trade-count and finalized-flag validation,
subscribe/unsubscribe payloads, control response parsing, supported intervals,
combined stream URLs, in-memory subscription state, fail-closed builder outputs,
finalized bar cache, and offline reconnect backoff calculation.

## Configuration

The plugin registers these KV defaults:

| Key | Default | Purpose |
| --- | --- | --- |
| `plugin.binance.rest_base` | `https://api.binance.com` | Public REST base for later market-data use. |
| `plugin.binance.ws_base` | `wss://stream.binance.com:9443/stream` | Public combined-stream WebSocket base. |
| `plugin.binance.symbols` | `BTCUSDT,ETHUSDT` | Initial public kline subscription symbols. |
| `plugin.binance.bar_seconds` | `300` | Kline interval selector. |
| `plugin.binance.ws_backoff_ms` | `1000` | Base reconnect backoff in milliseconds. |
| `plugin.binance.ws_backoff_cap_ms` | `60000` | Reconnect backoff cap in milliseconds. |
| `plugin.binance.max_symbols` | `128` | Symbol-cap placeholder for public subscriptions. |
| `plugin.binance.dry_run` | `1` | Guard for signed helpers. `1` logs/skips network requests; `0` requires explicit credentials and operator intent. |

Supported `bar_seconds` mappings are intentionally explicit:

| Seconds | Binance interval |
| ---: | --- |
| `60` | `1m` |
| `180` | `3m` |
| `300` | `5m` |
| `900` | `15m` |
| `1800` | `30m` |
| `3600` | `1h` |
| `7200` | `2h` |
| `14400` | `4h` |
| `21600` | `6h` |
| `28800` | `8h` |
| `43200` | `12h` |
| `86400` | `1d` |
| `259200` | `3d` |
| `604800` | `1w` |

Unsupported intervals and too-small interval output buffers fail closed in both
configuration-derived planning and raw public WebSocket parser/build helpers.

## Admin commands

| Command | Behavior |
| --- | --- |
| `binance status` | Shows subscription count, finalized-bar cache count, dry-run flag, and scaffold WS state. |
| `binance subscribe <symbol>` | Adds a public market-data symbol and reports the prepared kline stream name. |
| `binance unsubscribe <symbol>` | Removes a public market-data symbol from the local subscription table. |
| `binance subscriptions` | Lists active public market-data symbols. |
| `binance plan` | Shows an offline public WebSocket connection plan: count, interval, request id, subscribe payload length, backoff settings, and a truncated public combined stream URL preview. |

These commands only mutate in-process public subscription state or print offline
plans. They do not contact Binance.

## Test coverage

The focused Binance tests cover:

- public kline frame parsing, root/data/k-object/event/open/close integer-timestamp/symbol/string-field/finite-OHLCV/trade-count/finalized-flag validation, supported-interval validation/fail-closed output behavior, data/kline symbol validation, stream parser/builder symbol validation and fail-closed output behavior, fail-closed URL/payload builder output behavior, and combined-stream type/symbol/interval mismatch rejection;
- finalized bar cache upsert/get behavior;
- subscription add/remove/snapshot behavior;
- subscribe/unsubscribe/list payload builders;
- public WebSocket control response parsing, including fail-closed root-object, request id, error code, error message, and result-list stream validation;
- combined stream URL builders;
- offline connection-plan composition;
- market-data dispatch into subscription and cache state;
- deterministic capped reconnect backoff;
- offline HMAC-SHA256 signing and signed-query construction.

Run:

```sh
meson setup build
meson compile -C build
meson test -C build
```

## Non-goals

This scaffold does not implement:

- broker/router integration;
- live-trading deployment;
- private Binance WebSocket APIs;
- account balance parsing or position accounting;
- strategy selection;
- persistence;
- runtime deployment of a WebSocket pump.

Live execution still requires separate design, review, global Binance rate-limit
coordination, secrets handling, audit logging, and explicit authorization before
any strategy should call these helpers with dry run disabled.
