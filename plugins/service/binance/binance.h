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
#define BNB_CONTROL_MSG_SZ  192
#define BNB_WS_BACKOFF_CAP_MS 60000

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

// Fixed-size public market-data subscription registry.
typedef struct
{
  bnb_sub_t subs[BNB_MAX_SYMBOLS];
  uint32_t  count;
  pthread_rwlock_t rwl;
} bnb_subscription_table_t;

// Fixed-size finalized-bar cache for the public market-data path.
typedef struct
{
  bnb_bar_t bars[BNB_MAX_SYMBOLS];
  uint32_t  count;
  pthread_mutex_t mu;
} bnb_bar_cache_t;

typedef enum
{
  BNB_WS_CONTROL_UNKNOWN = 0,
  BNB_WS_CONTROL_ACK,
  BNB_WS_CONTROL_ERROR,
  BNB_WS_CONTROL_RESULT_LIST,
} bnb_ws_control_kind_t;

// Parsed response for public WebSocket control commands.
typedef struct
{
  bnb_ws_control_kind_t kind;
  uint32_t request_id;
  int32_t code;
  uint32_t result_count;
  char msg[BNB_CONTROL_MSG_SZ];
} bnb_ws_control_response_t;

// Offline WebSocket connection plan for public kline subscriptions.
typedef struct
{
  char url[BNB_URL_SZ];
  char subscribe_payload[BNB_WS_PAYLOAD_SZ];
  char interval[BNB_INTERVAL_SZ];
  uint32_t symbol_count;
  uint32_t request_id;
} bnb_ws_connection_plan_t;

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
static void bnb_cmd_subscriptions(const cmd_ctx_t *ctx);
static void bnb_cmd_plan(const cmd_ctx_t *ctx);
#endif

// -----------------------------------------------------------------------
// Market-data-only helpers (Phase 2b)
// -----------------------------------------------------------------------

bool bnb_ws_parse_kline_frame(const char *frame, bnb_bar_t *out,
    char *interval, size_t interval_sz);
bool bnb_ws_parse_control_response(const char *frame,
    bnb_ws_control_response_t *out);
bool bnb_ws_build_subscribe_payload(const char * const *symbols,
    uint32_t symbol_count, const char *interval, uint32_t request_id,
    char *out, size_t out_sz);
bool bnb_ws_build_unsubscribe_payload(const char * const *symbols,
    uint32_t symbol_count, const char *interval, uint32_t request_id,
    char *out, size_t out_sz);
bool bnb_ws_build_list_subscriptions_payload(uint32_t request_id,
    char *out, size_t out_sz);
bool bnb_ws_build_combined_stream_url(const char *base_url,
    const char * const *symbols, uint32_t symbol_count, const char *interval,
    char *out, size_t out_sz);
bool bnb_ws_build_stream_name(const char *symbol, const char *interval,
    char *out, size_t out_sz);
bool bnb_ws_parse_stream_name(const char *stream, char *symbol, size_t symbol_sz,
    char *interval, size_t interval_sz);
bool bnb_interval_from_bar_seconds(uint32_t bar_seconds, char *out, size_t out_sz);
bool bnb_ws_reconnect_backoff_ms(uint32_t base_ms, uint32_t attempt,
    uint32_t cap_ms, uint32_t *out_ms);

void bnb_bar_cache_init(bnb_bar_cache_t *cache);
void bnb_bar_cache_destroy(bnb_bar_cache_t *cache);
bool bnb_bar_cache_upsert(bnb_bar_cache_t *cache, const bnb_bar_t *bar);
bool bnb_bar_cache_get(const bnb_bar_cache_t *cache, const char *symbol,
    bnb_bar_t *out);
uint32_t bnb_bar_cache_count(const bnb_bar_cache_t *cache);

void bnb_subscription_table_init(bnb_subscription_table_t *table);
void bnb_subscription_table_destroy(bnb_subscription_table_t *table);
bool bnb_subscription_table_add(bnb_subscription_table_t *table, const char *symbol);
uint32_t bnb_subscription_table_add_csv(bnb_subscription_table_t *table,
    const char *symbols_csv);
bool bnb_subscription_table_remove(bnb_subscription_table_t *table, const char *symbol);
bool bnb_subscription_table_contains(const bnb_subscription_table_t *table, const char *symbol);
uint32_t bnb_subscription_table_count(const bnb_subscription_table_t *table);
uint32_t bnb_subscription_table_snapshot(const bnb_subscription_table_t *table,
    char symbols[][BNB_SYMBOL_SZ], uint32_t max_symbols);
bool bnb_subscription_table_update_bar(bnb_subscription_table_t *table,
    const bnb_bar_t *bar);
bool bnb_subscription_table_get_bar(const bnb_subscription_table_t *table,
    const char *symbol, bnb_bar_t *out);
bool bnb_subscription_table_build_subscribe_payload(
    const bnb_subscription_table_t *table, const char *interval,
    uint32_t request_id, char *out, size_t out_sz);
bool bnb_subscription_table_build_unsubscribe_payload(
    const bnb_subscription_table_t *table, const char *interval,
    uint32_t request_id, char *out, size_t out_sz);
bool bnb_subscription_table_build_combined_stream_url(
    const bnb_subscription_table_t *table, const char *base_url,
    const char *interval, char *out, size_t out_sz);
bool bnb_ws_build_connection_plan(const bnb_subscription_table_t *table,
    const char *base_url, uint32_t bar_seconds, uint32_t request_id,
    bnb_ws_connection_plan_t *out);
bool bnb_market_data_apply_kline_frame(bnb_subscription_table_t *table,
    bnb_bar_cache_t *cache, const char *frame, const char *expected_interval,
    bnb_bar_t *out);

#endif // BNB_INTERNAL

#endif // BM_BINANCE_H
