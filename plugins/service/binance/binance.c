// =======================================================================
// Binance service plugin (market-data scaffold — Phase 2b).
//
// Provides:
//   - Public Binance Spot market-data plumbing.
//   - Public kline stream parsing and finalized OHLCV cache helpers.
//   - Symbol subscription payload preparation.
//
// This file intentionally does not implement private/signed endpoints,
// account state, or order placement.
//
// Modeled on plugins/service/coinmarketcap/coinmarketcap.c.
// =======================================================================

#define BNB_INTERNAL
#define BNB_PLUGIN_MAIN
#include "binance.h"

#include <stdio.h>
#include <stdlib.h>

// -----------------------------------------------------------------------
// KV schema
// -----------------------------------------------------------------------

static const plugin_kv_entry_t bnb_kv_schema[] = {
  // REST + WS endpoints (overridable for testnet / regional mirrors).
  { "plugin.binance.rest_base",        KV_STR,    BNB_REST_BASE },
  { "plugin.binance.ws_base",          KV_STR,    BNB_WS_BASE   },

  // Default subscription set (comma-separated symbols, uppercase).
  { "plugin.binance.symbols",          KV_STR,    "BTCUSDT,ETHUSDT" },

  // Bar size in seconds (5m default — matches goldbucket_5m_builder).
  { "plugin.binance.bar_seconds",      KV_UINT32, "300" },

  // Reconnect backoff for the WS pump, in ms.
  { "plugin.binance.ws_backoff_ms",    KV_UINT32, "1000" },

  // Cap on concurrent symbol subscriptions.
  { "plugin.binance.max_symbols",      KV_UINT32, "128" },

  // Dry-run guard placeholder. This scaffold does not implement private
  // execution endpoints.
  { "plugin.binance.dry_run",          KV_UINT8,  "1" },
};

// -----------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------

// Public market-data subscription table.
static bnb_subscription_table_t bnb_subscriptions;

// WS pump task handle.
static task_t         *bnb_ws_task = NULL;

// Latest finalized public market-data bars.
static bnb_bar_cache_t bnb_bar_cache;

// -----------------------------------------------------------------------
// Admin commands
// -----------------------------------------------------------------------

// !binance status — show subscribed symbols, last bar timestamps, dry_run flag.
static void
bnb_cmd_status(const cmd_ctx_t *ctx)
{
  char    buf[BNB_REPLY_SZ];
  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");

  uint32_t count = bnb_subscription_table_count(&bnb_subscriptions);

  snprintf(buf, sizeof(buf),
      "binance: subs=%u cache=%u dry_run=%u ws=%s market-data-only",
      count, bnb_bar_cache_count(&bnb_bar_cache), dry,
      bnb_ws_task != NULL ? "running" : "stopped");

  cmd_reply(ctx, buf);
}

// !binance subscribe SYMBOL — add a symbol to the subscription set.
static void
bnb_cmd_subscribe(const cmd_ctx_t *ctx)
{
  const char *symbol = NULL;
  char interval[BNB_INTERVAL_SZ];
  char stream[BNB_STREAM_SZ];

  if(ctx != NULL && ctx->parsed != NULL && ctx->parsed->argc > 0)
    symbol = ctx->parsed->argv[0];

  if(!bnb_interval_from_bar_seconds((uint32_t)kv_get_uint("plugin.binance.bar_seconds"),
        interval, sizeof(interval)))
  {
    cmd_reply(ctx, "binance subscribe: unsupported bar_seconds");
    return;
  }

  if(symbol == NULL || !bnb_subscription_table_add(&bnb_subscriptions, symbol)
      || !bnb_ws_build_stream_name(symbol, interval, stream, sizeof(stream)))
  {
    cmd_reply(ctx, "binance subscribe: invalid symbol");
    return;
  }

  // Phase 2b remains market-data-only and offline by default. Runtime WS
  // connection plumbing is intentionally separate from this parser/cache PR.
  char buf[BNB_REPLY_SZ];
  snprintf(buf, sizeof(buf), "binance subscribe prepared: %s (market-data only)", stream);
  cmd_reply(ctx, buf);
}

// !binance unsubscribe SYMBOL — remove a symbol from the subscription set.
static void
bnb_cmd_unsubscribe(const cmd_ctx_t *ctx)
{
  const char *symbol = NULL;

  if(ctx != NULL && ctx->parsed != NULL && ctx->parsed->argc > 0)
    symbol = ctx->parsed->argv[0];

  if(symbol == NULL || !bnb_subscription_table_remove(&bnb_subscriptions, symbol))
  {
    cmd_reply(ctx, "binance unsubscribe: symbol not subscribed");
    return;
  }

  cmd_reply(ctx, "binance unsubscribe prepared (market-data only)");
}

// !binance subscriptions — list the active public market-data symbols.
static void
bnb_cmd_subscriptions(const cmd_ctx_t *ctx)
{
  char symbols[BNB_MAX_SYMBOLS][BNB_SYMBOL_SZ];
  char buf[BNB_REPLY_SZ];
  uint32_t count;
  uint32_t copied;
  uint32_t i;
  size_t off;
  bool truncated = false;

  count = bnb_subscription_table_count(&bnb_subscriptions);
  copied = bnb_subscription_table_snapshot(&bnb_subscriptions,
      symbols, BNB_MAX_SYMBOLS);

  off = (size_t)snprintf(buf, sizeof(buf),
      "binance subscriptions: count=%u symbols=", count);

  if(count == 0)
  {
    cmd_reply(ctx, "binance subscriptions: count=0");
    return;
  }

  for(i = 0; i < copied; i++)
  {
    int n;

    if(off >= sizeof(buf))
    {
      truncated = true;
      break;
    }

    n = snprintf(buf + off, sizeof(buf) - off, "%s%s",
        i == 0 ? "" : ",", symbols[i]);
    if(n < 0 || (size_t)n >= sizeof(buf) - off)
    {
      truncated = true;
      break;
    }
    off += (size_t)n;
  }

  if(truncated && off + 4 < sizeof(buf))
    snprintf(buf + off, sizeof(buf) - off, ",...");

  cmd_reply(ctx, buf);
}

// -----------------------------------------------------------------------
// Plugin lifecycle
// -----------------------------------------------------------------------

// Initialize the binance plugin. Registers admin commands and clears
// the subscription table. Does NOT open any network sockets — that
// happens in start().
// returns: SUCCESS or FAIL
static bool
bnb_init(void)
{
  uint32_t default_subs = 0;
  const char *symbols_csv = NULL;

  bnb_bar_cache_init(&bnb_bar_cache);
  bnb_subscription_table_init(&bnb_subscriptions);

  if(cmd_register("binance", "binance.status",
      "binance status",
      "Show binance plugin connection status",
      NULL,
      USERNS_GROUP_ADMIN, 0, CMD_SCOPE_ANY, METHOD_T_ANY,
      bnb_cmd_status, NULL,
      NULL, NULL, NULL, 0) != SUCCESS)
    return(FAIL);

  if(cmd_register("binance", "binance.subscribe",
      "binance subscribe <symbol>",
      "Add a symbol to the binance subscription set",
      NULL,
      USERNS_GROUP_ADMIN, 0, CMD_SCOPE_ANY, METHOD_T_ANY,
      bnb_cmd_subscribe, NULL,
      NULL, NULL, NULL, 0) != SUCCESS)
  {
    cmd_unregister("binance.status");
    return(FAIL);
  }

  if(cmd_register("binance", "binance.unsubscribe",
      "binance unsubscribe <symbol>",
      "Remove a symbol from the binance subscription set",
      NULL,
      USERNS_GROUP_ADMIN, 0, CMD_SCOPE_ANY, METHOD_T_ANY,
      bnb_cmd_unsubscribe, NULL,
      NULL, NULL, NULL, 0) != SUCCESS)
  {
    cmd_unregister("binance.status");
    cmd_unregister("binance.subscribe");
    return(FAIL);
  }

  if(cmd_register("binance", "binance.subscriptions",
      "binance subscriptions",
      "List active binance public market-data subscriptions",
      NULL,
      USERNS_GROUP_ADMIN, 0, CMD_SCOPE_ANY, METHOD_T_ANY,
      bnb_cmd_subscriptions, NULL,
      NULL, NULL, NULL, 0) != SUCCESS)
  {
    cmd_unregister("binance.status");
    cmd_unregister("binance.subscribe");
    cmd_unregister("binance.unsubscribe");
    return(FAIL);
  }

  symbols_csv = kv_get_str("plugin.binance.symbols");
  default_subs = bnb_subscription_table_add_csv(&bnb_subscriptions, symbols_csv);

  clam(CLAM_INFO, BNB_CTX, "binance plugin initialized (market-data-only subs=%u)",
      default_subs);

  return(SUCCESS);
}

// Begin active operation. Phase 2b still leaves live network loops dormant;
// parser/cache functions are exercised through offline tests.
// returns: SUCCESS or FAIL
static bool
bnb_start(void)
{
  clam(CLAM_INFO, BNB_CTX, "binance start (market-data parser/cache only)");
  return(SUCCESS);
}

// Drain in-flight work. Phase 2b no-op.
// returns: SUCCESS or FAIL
static bool
bnb_stop(void)
{
  if(bnb_ws_task != NULL)
  {
    // task_cancel(bnb_ws_task);
    bnb_ws_task = NULL;
  }
  clam(CLAM_INFO, BNB_CTX, "binance stop (scaffold no-op)");
  return(SUCCESS);
}

// Final cleanup. Unregisters commands, destroys synchronization primitives.
static void
bnb_deinit(void)
{
  cmd_unregister("binance.status");
  cmd_unregister("binance.subscribe");
  cmd_unregister("binance.unsubscribe");
  cmd_unregister("binance.subscriptions");

  bnb_bar_cache_destroy(&bnb_bar_cache);
  bnb_subscription_table_destroy(&bnb_subscriptions);

  clam(CLAM_INFO, BNB_CTX, "binance plugin deinitialized");
}

// -----------------------------------------------------------------------
// Plugin descriptor
// -----------------------------------------------------------------------

const plugin_desc_t bm_plugin_desc = {
  .api_version     = PLUGIN_API_VERSION,
  .name            = "binance",
  .version         = "0.1-scaffold",
  .type            = PLUGIN_SERVICE,
  .kind            = "binance",
  .provides        = { { .name = "service_binance" } },
  .provides_count  = 1,
  .requires_count  = 0,
  .kv_schema       = bnb_kv_schema,
  .kv_schema_count = sizeof(bnb_kv_schema) / sizeof(bnb_kv_schema[0]),
  .init            = bnb_init,
  .start           = bnb_start,
  .stop            = bnb_stop,
  .deinit          = bnb_deinit,
  .ext             = NULL,
};
