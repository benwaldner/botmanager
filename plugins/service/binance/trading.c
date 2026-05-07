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

  snprintf(s_api_key,    sizeof(s_api_key),    "%s", key);
  snprintf(s_api_secret, sizeof(s_api_secret), "%s", sec);
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

// Build a signed POST request and submit it.  Returns false on error.
static bool
trading_submit_signed_post(const char        *path,
                           const char        *base_params,
                           curl_done_cb_t     cb,
                           void              *user_data)
{
  char signed_qs[BNB_SIGNED_QS_SZ];
  if(!bnb_build_signed_query(base_params, s_api_secret, 0,
                             signed_qs, sizeof(signed_qs)))
  {
    clam(CLAM_WARN, BNB_CTX, "trading: failed to build signed query for %s", path);
    return(false);
  }

  char url[BNB_URL_SZ];
  snprintf(url, sizeof(url), "%s%s", BNB_REST_BASE, path);

  curl_request_t *req = curl_request_create(CURL_METHOD_POST, url, cb, user_data);
  if(!req)
    return(false);

  if(!curl_request_set_body(req, "application/x-www-form-urlencoded",
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

// -----------------------------------------------------------------------
// Public API — order placement
// -----------------------------------------------------------------------

// Place an async MARKET SELL for `qty` of `symbol`.
// When dry_run=1 (default), logs the intent and returns true without
// submitting any network request.
// `cb` is invoked on the curl worker thread when the response arrives.
// Returns false if credentials are missing, dry_run is off but key is
// empty, or the curl request cannot be submitted.
bool
bnb_order_market_sell(const char      *symbol,
                      double           qty,
                      const char      *client_order_id,
                      bnb_order_cb_t   cb,
                      void            *user_data)
{
  if(!symbol || qty <= 0.0)
    return(false);

  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");
  if(dry)
  {
    clam(CLAM_INFO, BNB_CTX,
         "DRY_RUN MARKET SELL %.8f %s coid=%s",
         qty, symbol, client_order_id ? client_order_id : "");
    return(true);
  }

  if(!s_credentials_loaded)
  {
    clam(CLAM_WARN, BNB_CTX,
         "trading: no credentials — cannot place MARKET SELL for %s", symbol);
    return(false);
  }

  // Build base params (quantity intentionally avoids trailing zeros).
  char base[BNB_SIGNED_QS_SZ / 2];
  int  n = snprintf(base, sizeof(base),
                    "symbol=%s&side=SELL&type=MARKET&quantity=%.8g"
                    "&newOrderRespType=RESULT",
                    symbol, qty);
  if(client_order_id && *client_order_id)
    snprintf(base + n, sizeof(base) - (size_t)n,
             "&newClientOrderId=%s", client_order_id);

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

  if(!trading_submit_signed_post("/api/v3/order", base,
                                 trading_order_response_cb, ctx))
  {
    // ctx already freed inside trading_submit_signed_post on failure.
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
  if(!symbol || !client_order_id || !*client_order_id)
    return(false);

  uint8_t dry = (uint8_t)kv_get_uint("plugin.binance.dry_run");
  if(dry)
  {
    clam(CLAM_INFO, BNB_CTX,
         "DRY_RUN CANCEL %s coid=%s", symbol, client_order_id);
    return(true);
  }

  if(!s_credentials_loaded)
  {
    clam(CLAM_WARN, BNB_CTX,
         "trading: no credentials — cannot cancel order %s", client_order_id);
    return(false);
  }

  char base[BNB_SIGNED_QS_SZ / 2];
  snprintf(base, sizeof(base),
           "symbol=%s&origClientOrderId=%s", symbol, client_order_id);

  bnb_order_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if(!ctx)
    return(false);

  snprintf(ctx->symbol,          sizeof(ctx->symbol),          "%s", symbol);
  snprintf(ctx->client_order_id, sizeof(ctx->client_order_id),
           "%s", client_order_id);
  ctx->cb        = cb;
  ctx->user_data = user_data;

  clam(CLAM_INFO, BNB_CTX, "cancelling order %s %s", symbol, client_order_id);

  if(!trading_submit_signed_post("/api/v3/order", base,
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
    return(true);
  }

  if(!s_credentials_loaded)
  {
    clam(CLAM_WARN, BNB_CTX, "trading: no credentials — cannot query account");
    return(false);
  }

  char signed_qs[BNB_SIGNED_QS_SZ];
  if(!bnb_build_signed_query("omitZeroBalances=true", s_api_secret, 0,
                             signed_qs, sizeof(signed_qs)))
    return(false);

  char url[BNB_URL_SZ * 2];
  snprintf(url, sizeof(url), "%s/api/v3/account?%s", BNB_REST_BASE, signed_qs);

  bnb_account_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if(!ctx)
    return(false);

  ctx->cb        = cb;
  ctx->user_data = user_data;

  char hdr[BNB_APIKEY_SZ + 32];
  snprintf(hdr, sizeof(hdr), "X-MBX-APIKEY: %s", s_api_key);

  curl_request_t *req = curl_request_create(CURL_METHOD_GET, url,
                                             trading_account_response_cb, ctx);
  if(!req)
  {
    free(ctx);
    return(false);
  }

  if(!curl_request_add_header(req, hdr))
  {
    free(ctx);
    return(false);
  }

  if(!curl_request_submit(req))
  {
    free(ctx);
    return(false);
  }

  return(true);
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
