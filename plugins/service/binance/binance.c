// =======================================================================
// Binance service plugin (scaffold — Phase 2a).
//
// Provides:
//   - REST + WebSocket connector to Binance Spot (api.binance.com,
//     stream.binance.com:9443)
//   - Symbol subscription registry (1m kline aggregated to 5m bars)
//   - Event publish on bar finalize (consumed by trading bot kind)
//
// This file is the skeleton: lifecycle, KV schema, admin commands.
// Network code (REST poll, WS subscribe, bar accumulator) lands in
// Phase 2b — every TODO marker below points to the next slice of work.
//
// Modeled on plugins/service/coinmarketcap/coinmarketcap.c.
// =======================================================================

#define BNB_INTERNAL
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

  // Dry-run guard placeholder. This scaffold does not implement signed,
  // private, or order-placement endpoints.
  { "plugin.binance.dry_run",          KV_UINT8,  "1" },
};

// -----------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------

// Subscription table. Static-sized for now — resized on demand in 2b.
static bnb_sub_t       bnb_subs[BNB_MAX_SYMBOLS];
static uint32_t        bnb_sub_count = 0;
static pthread_rwlock_t bnb_subs_rwl;

// WS pump task handle.
static task_t         *bnb_ws_task = NULL;

// -----------------------------------------------------------------------
// Admin commands (skeleton replies — real handlers in 2b)
// -----------------------------------------------------------------------

// !binance status — show subscribed symbols, last bar timestamps, dry_run flag.
static void
bnb_cmd_status(const cmd_ctx_t *ctx)
{
  char    buf[BNB_REPLY_SZ];
  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");

  pthread_rwlock_rdlock(&bnb_subs_rwl);
  uint32_t count = bnb_sub_count;
  pthread_rwlock_unlock(&bnb_subs_rwl);

  snprintf(buf, sizeof(buf),
      "binance: subs=%u dry_run=%u ws=%s",
      count, dry, bnb_ws_task != NULL ? "running" : "stopped");

  cmd_reply(ctx, buf);
}

// !binance subscribe SYMBOL — add a symbol to the subscription set.
static void
bnb_cmd_subscribe(const cmd_ctx_t *ctx)
{
  // TODO Phase 2b: parse args, uppercase symbol, append to bnb_subs[],
  // re-arm WS subscription frame.
  cmd_reply(ctx, "binance subscribe: not yet implemented (Phase 2b)");
}

// !binance unsubscribe SYMBOL — remove a symbol from the subscription set.
static void
bnb_cmd_unsubscribe(const cmd_ctx_t *ctx)
{
  // TODO Phase 2b: remove from bnb_subs[], re-arm WS unsubscribe frame.
  cmd_reply(ctx, "binance unsubscribe: not yet implemented (Phase 2b)");
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
  pthread_rwlock_init(&bnb_subs_rwl, NULL);
  memset(bnb_subs, 0, sizeof(bnb_subs));
  bnb_sub_count = 0;

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

  clam(CLAM_INFO, BNB_CTX, "binance plugin initialized (scaffold)");

  return(SUCCESS);
}

// Begin active operation. Phase 2a leaves WS + REST loops dormant —
// real implementations land in 2b.
// returns: SUCCESS or FAIL
static bool
bnb_start(void)
{
  // TODO Phase 2b: open WS connection, subscribe to default symbols
  // from KV "plugin.binance.symbols", spawn bar-accumulator task, wire
  // on-finalize event-bus publish.
  clam(CLAM_INFO, BNB_CTX, "binance start (scaffold no-op — WS pump not yet wired)");
  return(SUCCESS);
}

// Drain in-flight work. Phase 2a no-op.
// returns: SUCCESS or FAIL
static bool
bnb_stop(void)
{
  // TODO Phase 2b: close WS, flush partial bar, cancel REST inflight.
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

  pthread_rwlock_destroy(&bnb_subs_rwl);

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
