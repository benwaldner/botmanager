#ifndef BM_BINANCE_H
#define BM_BINANCE_H

// No public API — this plugin is loaded via dlopen and interacts
// with the core exclusively through cmd_register / curl_request_* /
// kv_get_* / sock_*. All declarations are internal.

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
#include <string.h>
#include <strings.h>
#include <time.h>

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------

#define BNB_CTX             "binance"

// API base URLs.
#define BNB_REST_BASE       "https://api.binance.com"
#define BNB_REST_TICKER_24H BNB_REST_BASE "/api/v3/ticker/24hr"
#define BNB_REST_EXCHANGE   BNB_REST_BASE "/api/v3/exchangeInfo"
#define BNB_WS_BASE         "wss://stream.binance.com:9443/stream"

// Size limits.
#define BNB_SYMBOL_SZ       16
#define BNB_URL_SZ          512
#define BNB_REPLY_SZ        640
#define BNB_MAX_SYMBOLS     128

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

// -----------------------------------------------------------------------
// Forward declarations (lifecycle + commands)
// -----------------------------------------------------------------------

static bool bnb_init(void);
static bool bnb_start(void);
static bool bnb_stop(void);
static void bnb_deinit(void);

static void bnb_cmd_status(const cmd_ctx_t *ctx);
static void bnb_cmd_subscribe(const cmd_ctx_t *ctx);
static void bnb_cmd_unsubscribe(const cmd_ctx_t *ctx);

#endif // BNB_INTERNAL

#endif // BM_BINANCE_H
