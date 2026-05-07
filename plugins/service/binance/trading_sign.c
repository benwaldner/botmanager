// =======================================================================
// Binance HMAC-SHA256 signing helpers — pure, no core dependencies.
//
// Split from trading.c so that tests can link this file standalone
// (without clam, kv, curl, or any other botmanager core symbol).
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Compute HMAC-SHA256(secret, msg) and write lowercase hex to `out`.
// `out` must be at least BNB_SIG_HEX_SZ (65) bytes.
// Returns true on success.
bool
bnb_hmac_sha256_hex(const char *secret, size_t secret_len,
                    const char *msg,    size_t msg_len,
                    char *out,          size_t out_sz)
{
  if(!secret || !msg || !out || out_sz < BNB_SIG_HEX_SZ)
    return(false);

  unsigned char digest[32];
  unsigned int  digest_len = 0;

  if(!HMAC(EVP_sha256(),
           secret, (int)secret_len,
           (const unsigned char *)msg, msg_len,
           digest, &digest_len)
     || digest_len != 32)
    return(false);

  for(unsigned int i = 0; i < digest_len; i++)
    snprintf(out + i * 2, 3, "%02x", (unsigned)digest[i]);

  out[64] = '\0';
  return(true);
}

// Build a Binance signed query string from `base_params`, appending
// &timestamp=<ms>&recvWindow=5000&signature=<hex>.
// Pass now_ms=0 to use time(NULL)*1000.
// Returns bytes written (excluding NUL), or 0 on failure.
size_t
bnb_build_signed_query(const char *base_params,
                       const char *secret,
                       int64_t     now_ms,
                       char       *out,
                       size_t      out_sz)
{
  if(!base_params || !secret || !out || out_sz < 2)
    return(0);

  if(now_ms <= 0)
    now_ms = (int64_t)time(NULL) * 1000;

  char qs[BNB_SIGNED_QS_SZ];
  int n = snprintf(qs, sizeof(qs),
                   "%s&timestamp=%lld&recvWindow=5000",
                   base_params, (long long)now_ms);
  if(n <= 0 || (size_t)n >= sizeof(qs))
    return(0);

  char sig[BNB_SIG_HEX_SZ];
  if(!bnb_hmac_sha256_hex(secret, strlen(secret),
                          qs, (size_t)n,
                          sig, sizeof(sig)))
    return(0);

  n = snprintf(out, out_sz, "%s&signature=%s", qs, sig);
  if(n <= 0 || (size_t)n >= out_sz)
    return(0);

  return((size_t)n);
}
