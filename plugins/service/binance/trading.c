// =======================================================================
// Binance signed-endpoint helpers — Phase 3 trading extension.
//
// Provides async order placement for Binance private REST endpoints.
// All order functions honour the `plugin.binance.dry_run` KV flag (default 1).
// When dry_run is non-zero, order calls are logged but no HTTP request is
// submitted.
//
// Pure signing functions (bnb_hmac_sha256_hex, bnb_build_signed_query) live
// in trading_sign.c to allow standalone test linking without core deps.
//
// Credentials are loaded from the environment at plugin start:
//   BINANCE_API_KEY     — API key for signed requests
//   BINANCE_API_SECRET  — HMAC-SHA256 signing secret
//
// Call bnb_trading_init() from bnb_init() and bnb_trading_deinit() from
// bnb_deinit(). This file is built as part of the binance shared library
// (see meson.build).
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// -----------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------

// Credentials loaded at init.  Zeroed on deinit.
static char s_api_key[BNB_APIKEY_SZ];
static char s_api_secret[BNB_APISECRET_SZ];
static bool s_credentials_loaded = false;

// -----------------------------------------------------------------------
// Credential helpers
// -----------------------------------------------------------------------

// Load KEY and SECRET from environment.  Returns true if both are present.
static bool
trading_load_credentials(void)
{
  const char *key = getenv("BINANCE_API_KEY");
  const char *sec = getenv("BINANCE_API_SECRET");

  if(!key || !*key || !sec || !*sec)
    return(false);
  if(strlen(key) >= sizeof(s_api_key) || strlen(sec) >= sizeof(s_api_secret))
  {
    clam(CLAM_WARN, BNB_CTX,
         "trading: BINANCE_API_KEY/SECRET too long for fixed buffers");
    return(false);
  }

  snprintf(s_api_key,    sizeof(s_api_key),    "%s", key);
  snprintf(s_api_secret, sizeof(s_api_secret), "%s", sec);
  return(true);
}

static const char *
trading_rest_base(void)
{
  const char *base = kv_get_str("plugin.binance.rest_base");
  if(base == NULL || base[0] == '\0')
    return(BNB_REST_BASE);
  return(base);
}

static bool
trading_symbol_valid(const char *symbol)
{
  size_t i;

  if(symbol == NULL || symbol[0] == '\0')
    return(false);

  for(i = 0; symbol[i] != '\0'; i++)
  {
    if(i + 1 >= BNB_SYMBOL_SZ)
      return(false);
    if(!isalnum((unsigned char)symbol[i]))
      return(false);
  }
  return(true);
}

static bool
trading_client_order_id_valid(const char *client_order_id)
{
  size_t i;

  if(client_order_id == NULL || client_order_id[0] == '\0')
    return(true);

  for(i = 0; client_order_id[i] != '\0'; i++)
  {
    unsigned char ch = (unsigned char)client_order_id[i];
    if(i + 1 >= BNB_CLIENT_ORDER_ID_SZ)
      return(false);
    if(!(isalnum(ch) || ch == '_' || ch == '-' || ch == '.'))
      return(false);
  }
  return(true);
}

// -----------------------------------------------------------------------
// Async signed-request context
// -----------------------------------------------------------------------

// Heap-allocated context threaded through curl callbacks.
typedef struct
{
  char             symbol[BNB_SYMBOL_SZ];
  char             client_order_id[BNB_CLIENT_ORDER_ID_SZ];
  bnb_order_cb_t   cb;
  void            *user_data;
} bnb_order_ctx_t;

// Generic curl completion handler for signed order requests.
static void
trading_order_response_cb(const curl_response_t *resp)
{
  bnb_order_ctx_t *ctx = (bnb_order_ctx_t *)resp->user_data;

  bnb_order_result_t result;
  memset(&result, 0, sizeof(result));
  result.symbol          = ctx->symbol;
  result.client_order_id = ctx->client_order_id;
  result.ok              = (resp->status == 200);
  result.http_status     = (int)resp->status;
  result.raw_response    = resp->body;

  if(ctx->cb)
    ctx->cb(&result, ctx->user_data);

  free(ctx);
}

// Build a signed request and submit it. Returns false on error.
static bool
trading_submit_signed_request(curl_method_t     method,
                              const char       *path,
                              const char       *base_params,
                              curl_done_cb_t    cb,
                              void             *user_data)
{
  char signed_qs[BNB_SIGNED_QS_SZ];
  if(!bnb_build_signed_query(base_params, s_api_secret, 0,
                             signed_qs, sizeof(signed_qs)))
  {
    clam(CLAM_WARN, BNB_CTX, "trading: failed to build signed query for %s", path);
    free(user_data);
    return(false);
  }

  char url[BNB_URL_SZ];
  int url_n;
  if(method == CURL_METHOD_POST)
    url_n = snprintf(url, sizeof(url), "%s%s", trading_rest_base(), path);
  else
    url_n = snprintf(url, sizeof(url), "%s%s?%s", trading_rest_base(), path, signed_qs);
  if(url_n <= 0 || (size_t)url_n >= sizeof(url))
  {
    free(user_data);
    return(false);
  }

  curl_request_t *req = curl_request_create(method, url, cb, user_data);
  if(!req)
  {
    free(user_data);
    return(false);
  }

  if(method == CURL_METHOD_POST
     && !curl_request_set_body(req, "application/x-www-form-urlencoded",
                               signed_qs, strlen(signed_qs)))
  {
    free(user_data);
    return(false);
  }

  char hdr[BNB_APIKEY_SZ + 32];
  snprintf(hdr, sizeof(hdr), "X-MBX-APIKEY: %s", s_api_key);
  if(!curl_request_add_header(req, hdr))
  {
    free(user_data);
    return(false);
  }

  if(!curl_request_submit(req))
  {
    free(user_data);
    return(false);
  }

  return(true);
}

static void
trading_order_dry_run_cb(const char      *symbol,
                         const char      *client_order_id,
                         bnb_order_cb_t   cb,
                         void            *user_data)
{
  if(cb == NULL)
    return;

  bnb_order_result_t result;
  memset(&result, 0, sizeof(result));
  result.symbol          = symbol;
  result.client_order_id = client_order_id ? client_order_id : "";
  result.ok              = true;
  result.http_status     = 0;
  result.raw_response    = "{\"dry_run\":true}";
  cb(&result, user_data);
}

static void
trading_account_dry_run_cb(bnb_account_cb_t cb, void *user_data)
{
  if(cb == NULL)
    return;

  bnb_account_result_t result;
  memset(&result, 0, sizeof(result));
  result.ok           = true;
  result.http_status  = 0;
  result.raw_response = "{\"dry_run\":true}";
  cb(&result, user_data);
}

// -----------------------------------------------------------------------
// Public API — order placement
// -----------------------------------------------------------------------

// Place an async MARKET SELL for `qty` of `symbol`.
// When dry_run=1 (default), logs the intent and returns true without
// submitting any network request. Dry-run callbacks are invoked synchronously;
// live HTTP callbacks arrive on the curl worker thread.
// Returns false if credentials are missing, dry_run is off but key is
// empty, or the curl request cannot be submitted.
bool
bnb_order_market_sell(const char      *symbol,
                      double           qty,
                      const char      *client_order_id,
                      bnb_order_cb_t   cb,
                      void            *user_data)
{
  if(!trading_symbol_valid(symbol) || qty <= 0.0
     || !trading_client_order_id_valid(client_order_id))
    return(false);

  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");
  if(dry)
  {
    clam(CLAM_INFO, BNB_CTX,
         "DRY_RUN MARKET SELL %.8f %s coid=%s",
         qty, symbol, client_order_id ? client_order_id : "");
    trading_order_dry_run_cb(symbol, client_order_id, cb, user_data);
    return(true);
  }

  if(!s_credentials_loaded)
  {
    clam(CLAM_WARN, BNB_CTX,
         "trading: no credentials — cannot place MARKET SELL for %s", symbol);
    return(false);
  }

  // Build base params with fixed decimal quantity to avoid scientific notation.
  char base[BNB_SIGNED_QS_SZ / 2];
  int  n = snprintf(base, sizeof(base),
                    "symbol=%s&side=SELL&type=MARKET&quantity=%.8f"
                    "&newOrderRespType=RESULT",
                    symbol, qty);
  if(n <= 0 || (size_t)n >= sizeof(base))
    return(false);
  if(client_order_id && *client_order_id)
  {
    int m = snprintf(base + n, sizeof(base) - (size_t)n,
                     "&newClientOrderId=%s", client_order_id);
    if(m <= 0 || (size_t)m >= sizeof(base) - (size_t)n)
      return(false);
  }

  bnb_order_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if(!ctx)
    return(false);

  snprintf(ctx->symbol,          sizeof(ctx->symbol),          "%s", symbol);
  snprintf(ctx->client_order_id, sizeof(ctx->client_order_id),
           "%s", client_order_id ? client_order_id : "");
  ctx->cb        = cb;
  ctx->user_data = user_data;

  clam(CLAM_INFO, BNB_CTX, "placing MARKET SELL %.8g %s coid=%s",
       qty, symbol, ctx->client_order_id);

  if(!trading_submit_signed_request(CURL_METHOD_POST, "/api/v3/order", base,
                                    trading_order_response_cb, ctx))
  {
    // ctx is freed inside trading_submit_signed_request on failure.
    return(false);
  }

  return(true);
}

// Cancel an open order by clientOrderId.  dry_run=1 logs and skips.
bool
bnb_order_cancel(const char      *symbol,
                 const char      *client_order_id,
                 bnb_order_cb_t   cb,
                 void            *user_data)
{
  if(!trading_symbol_valid(symbol) || !client_order_id || !*client_order_id
     || !trading_client_order_id_valid(client_order_id))
    return(false);

  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");
  if(dry)
  {
    clam(CLAM_INFO, BNB_CTX,
         "DRY_RUN CANCEL %s coid=%s", symbol, client_order_id);
    trading_order_dry_run_cb(symbol, client_order_id, cb, user_data);
    return(true);
  }

  if(!s_credentials_loaded)
  {
    clam(CLAM_WARN, BNB_CTX,
         "trading: no credentials — cannot cancel order %s", client_order_id);
    return(false);
  }

  char base[BNB_SIGNED_QS_SZ / 2];
  int n = snprintf(base, sizeof(base),
                   "symbol=%s&origClientOrderId=%s", symbol, client_order_id);
  if(n <= 0 || (size_t)n >= sizeof(base))
    return(false);

  bnb_order_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if(!ctx)
    return(false);

  snprintf(ctx->symbol,          sizeof(ctx->symbol),          "%s", symbol);
  snprintf(ctx->client_order_id, sizeof(ctx->client_order_id),
           "%s", client_order_id);
  ctx->cb        = cb;
  ctx->user_data = user_data;

  clam(CLAM_INFO, BNB_CTX, "cancelling order %s %s", symbol, client_order_id);

  if(!trading_submit_signed_request(CURL_METHOD_DELETE, "/api/v3/order", base,
                                    trading_order_response_cb, ctx))
    return(false);

  return(true);
}

// -----------------------------------------------------------------------
// Account balance query
// -----------------------------------------------------------------------

typedef struct
{
  bnb_account_cb_t  cb;
  void             *user_data;
} bnb_account_ctx_t;

static void
trading_account_response_cb(const curl_response_t *resp)
{
  bnb_account_ctx_t *ctx = (bnb_account_ctx_t *)resp->user_data;

  bnb_account_result_t result;
  memset(&result, 0, sizeof(result));
  result.ok           = (resp->status == 200);
  result.http_status  = (int)resp->status;
  result.raw_response = resp->body;

  if(ctx->cb)
    ctx->cb(&result, ctx->user_data);

  free(ctx);
}

// Async signed GET /api/v3/account — fetches balances.
// dry_run=1 logs and skips (no network call).
bool
bnb_account_query(bnb_account_cb_t cb, void *user_data)
{
  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");
  if(dry)
  {
    clam(CLAM_INFO, BNB_CTX, "DRY_RUN account query skipped");
    trading_account_dry_run_cb(cb, user_data);
    return(true);
  }

  if(!s_credentials_loaded)
  {
    clam(CLAM_WARN, BNB_CTX, "trading: no credentials — cannot query account");
    return(false);
  }

  bnb_account_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if(!ctx)
    return(false);

  ctx->cb        = cb;
  ctx->user_data = user_data;

  return(trading_submit_signed_request(CURL_METHOD_GET, "/api/v3/account",
                                       "omitZeroBalances=true",
                                       trading_account_response_cb, ctx));
}

// -----------------------------------------------------------------------
// Lifecycle — call from bnb_init() / bnb_deinit()
// -----------------------------------------------------------------------

bool
bnb_trading_init(void)
{
  memset(s_api_key,    0, sizeof(s_api_key));
  memset(s_api_secret, 0, sizeof(s_api_secret));
  s_credentials_loaded = trading_load_credentials();

  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");

  if(s_credentials_loaded)
    clam(CLAM_INFO, BNB_CTX,
         "trading init: credentials loaded, dry_run=%u", (unsigned)dry);
  else
    clam(CLAM_WARN, BNB_CTX,
         "trading init: no credentials (BINANCE_API_KEY/SECRET missing), "
         "dry_run=%u — order placement will fail when dry_run=0", (unsigned)dry);

  return(true);
}

void
bnb_trading_deinit(void)
{
  memset(s_api_key,    0, sizeof(s_api_key));
  memset(s_api_secret, 0, sizeof(s_api_secret));
  s_credentials_loaded = false;
  clam(CLAM_INFO, BNB_CTX, "trading deinit: credentials cleared");
}
