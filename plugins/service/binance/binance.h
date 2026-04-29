#ifndef BM_BINANCE_H
#define BM_BINANCE_H

// No exported C API — this plugin is loaded via dlopen and interacts
// with the core through plugin lifecycle hooks and commands. All
// declarations here are internal to the market-data-only Binance plugin.

#ifdef BNB_INTERNAL

#include "clam.h"
#include "cmd.h"
#include "common.h"
#include "curl.h"
#include "kv.h"
#include "mem.h"
#include "method.h"
#include "plugin.h"
#include "pool.h"
#include "task.h"

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <time.h>

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

#define BNB_CTX             "binance"

// Public market-data base URLs.
#define BNB_REST_BASE       "https://api.binance.com"
#define BNB_REST_TICKER_24H BNB_REST_BASE "/api/v3/ticker/24hr"
#define BNB_REST_EXCHANGE   BNB_REST_BASE "/api/v3/exchangeInfo"
#define BNB_WS_BASE         "wss://stream.binance.com:9443/stream"

// Size limits.
#define BNB_SYMBOL_SZ       16
#define BNB_URL_SZ          512
#define BNB_REPLY_SZ        640
#define BNB_MAX_SYMBOLS     128
#define BNB_INTERVAL_SZ     16
#define BNB_STREAM_SZ       64
#define BNB_WS_PAYLOAD_SZ   2048

// 5-minute bar size in seconds.
#define BNB_BAR_SECONDS     300

// -----------------------------------------------------------------------
// Data structures
// -----------------------------------------------------------------------

// In-memory 5m bar accumulator. One per subscribed symbol.
typedef struct
{
  char     symbol[BNB_SYMBOL_SZ];
  int64_t  open_time_ms;
  int64_t  close_time_ms;
  double   open;
  double   high;
  double   low;
  double   close;
  double   volume_base;
  double   volume_quote;
  uint32_t trade_count;
  bool     finalized;
} bnb_bar_t;

// Per-symbol subscription record.
typedef struct
{
  char         symbol[BNB_SYMBOL_SZ];
  bnb_bar_t    current_bar;
  pthread_mutex_t mu;
} bnb_sub_t;

// Fixed-size finalized-bar cache for the public market-data path.
typedef struct
{
  bnb_bar_t bars[BNB_MAX_SYMBOLS];
  uint32_t  count;
  pthread_mutex_t mu;
} bnb_bar_cache_t;

// -----------------------------------------------------------------------
// Forward declarations (lifecycle + commands)
// -----------------------------------------------------------------------

#ifdef BNB_PLUGIN_MAIN
static bool bnb_init(void);
static bool bnb_start(void);
static bool bnb_stop(void);
static void bnb_deinit(void);

static void bnb_cmd_status(const cmd_ctx_t *ctx);
static void bnb_cmd_subscribe(const cmd_ctx_t *ctx);
static void bnb_cmd_unsubscribe(const cmd_ctx_t *ctx);
#endif

// -----------------------------------------------------------------------
// Market-data-only helpers (Phase 2b)
// -----------------------------------------------------------------------

bool bnb_ws_parse_kline_frame(const char *frame, bnb_bar_t *out,
    char *interval, size_t interval_sz);
bool bnb_ws_build_subscribe_payload(const char * const *symbols,
    uint32_t symbol_count, const char *interval, uint32_t request_id,
    char *out, size_t out_sz);
bool bnb_ws_build_stream_name(const char *symbol, const char *interval,
    char *out, size_t out_sz);

void bnb_bar_cache_init(bnb_bar_cache_t *cache);
void bnb_bar_cache_destroy(bnb_bar_cache_t *cache);
bool bnb_bar_cache_upsert(bnb_bar_cache_t *cache, const bnb_bar_t *bar);
bool bnb_bar_cache_get(const bnb_bar_cache_t *cache, const char *symbol,
    bnb_bar_t *out);
uint32_t bnb_bar_cache_count(const bnb_bar_cache_t *cache);

#endif // BNB_INTERNAL

#endif // BM_BINANCE_H
