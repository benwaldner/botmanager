# Binance Public Market-Data Scaffold

This plugin is a market-data-only Binance Spot scaffold. It prepares and
parses public kline WebSocket data for BotManager, but it does not enable
trading or private account access.

## Safety boundaries

- No order placement.
- No account-state reads.
- No private or signed Binance endpoints.
- No API keys, signatures, HMACs, or credential handling.
- No live-trading router integration.
- No socket loop is started by the current scaffold.

The only Binance paths represented here are public market-data helpers for
kline stream names, combined-stream wrapper validation, fail-closed timestamp
and OHLCV field validation, subscribe/unsubscribe payloads, control response
parsing, combined stream URLs, in-memory subscription state, finalized bar
cache, and offline reconnect backoff calculation.

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
| `plugin.binance.dry_run` | `1` | Guard placeholder; private execution is not implemented. |

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

Unsupported intervals fail closed.

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

- public kline frame parsing, timestamp/OHLCV validation, and combined-stream symbol/interval mismatch rejection;
- finalized bar cache upsert/get behavior;
- subscription add/remove/snapshot behavior;
- subscribe/unsubscribe/list payload builders;
- public WebSocket control response parsing;
- combined stream URL builders;
- offline connection-plan composition;
- market-data dispatch into subscription and cache state;
- deterministic capped reconnect backoff.

Run:

```sh
meson setup build
meson compile -C build
meson test -C build
```

## Non-goals

This scaffold does not implement:

- live trading;
- broker or exchange execution;
- private Binance REST/WebSocket APIs;
- account balances or positions;
- order signing or cancellation;
- strategy selection;
- persistence;
- runtime deployment of a WebSocket pump.

Those capabilities require separate design, review, rate-limit coordination,
secrets handling, audit logging, and explicit authorization before they should
exist in this codebase.
