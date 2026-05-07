// Tests for the Binance signed-endpoint helpers in trading.c.
//
// All tests are offline (no network calls, no credentials). They exercise
// the pure signing functions bnb_hmac_sha256_hex() and
// bnb_build_signed_query() using known test vectors.

#define BNB_INTERNAL
#include "../../../../plugins/service/binance/binance.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------
// bnb_hmac_sha256_hex — known HMAC-SHA256 test vectors
// -----------------------------------------------------------------------

// Standard RFC 2104 / NIST vector:
// key     = "key"
// message = "The quick brown fox jumps over the lazy dog"
// digest  = f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8
static void
test_hmac_sha256_rfc_vector(void)
{
  const char *key = "key";
  const char *msg = "The quick brown fox jumps over the lazy dog";
  const char *expected =
    "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8";

  char out[BNB_SIG_HEX_SZ];
  assert(bnb_hmac_sha256_hex(key, strlen(key), msg, strlen(msg),
                             out, sizeof(out)));
  assert(strcmp(out, expected) == 0);
}

// Empty message.
static void
test_hmac_sha256_empty_message(void)
{
  const char *key      = "secret";
  const char *msg      = "";
  const char *expected =
    "f9e66e179b6747ae54108f82f8ade8b3c25d76fd30afde6c395822c530196169";

  char out[BNB_SIG_HEX_SZ];
  assert(bnb_hmac_sha256_hex(key, strlen(key), msg, 0, out, sizeof(out)));
  assert(strcmp(out, expected) == 0);
}

// Output buffer too small — must return false.
static void
test_hmac_sha256_rejects_small_output(void)
{
  char out[63];  // one byte short
  assert(!bnb_hmac_sha256_hex("k", 1, "m", 1, out, sizeof(out)));
}

// NULL inputs — must return false.
static void
test_hmac_sha256_rejects_null_inputs(void)
{
  char out[BNB_SIG_HEX_SZ];
  assert(!bnb_hmac_sha256_hex(NULL, 0, "msg", 3, out, sizeof(out)));
  assert(!bnb_hmac_sha256_hex("key", 3, NULL, 0, out, sizeof(out)));
  assert(!bnb_hmac_sha256_hex("key", 3, "msg", 3, NULL, sizeof(out)));
}

// Output is exactly 64 hex chars + NUL.
static void
test_hmac_sha256_output_length(void)
{
  char out[BNB_SIG_HEX_SZ];
  assert(bnb_hmac_sha256_hex("k", 1, "m", 1, out, sizeof(out)));
  assert(strlen(out) == 64);
}

// Output is lowercase hex (no uppercase, no non-hex chars).
static void
test_hmac_sha256_output_is_lowercase_hex(void)
{
  char out[BNB_SIG_HEX_SZ];
  assert(bnb_hmac_sha256_hex("k", 1, "m", 1, out, sizeof(out)));
  for(int i = 0; i < 64; i++)
  {
    char c = out[i];
    assert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
  }
}

// -----------------------------------------------------------------------
// bnb_build_signed_query — structure and signature correctness
// -----------------------------------------------------------------------

// Builds a query that contains timestamp, recvWindow, and a signature.
static void
test_signed_query_contains_required_fields(void)
{
  char out[BNB_SIGNED_QS_SZ];
  size_t n = bnb_build_signed_query("symbol=BTCUSDT&side=BUY",
                                    "testsecret", 1499827319559LL,
                                    out, sizeof(out));
  assert(n > 0);
  assert(strstr(out, "symbol=BTCUSDT") != NULL);
  assert(strstr(out, "side=BUY")       != NULL);
  assert(strstr(out, "timestamp=")     != NULL);
  assert(strstr(out, "recvWindow=5000") != NULL);
  assert(strstr(out, "signature=")     != NULL);
}

// The signature is exactly 64 hex chars.
static void
test_signed_query_signature_is_64_hex_chars(void)
{
  char out[BNB_SIGNED_QS_SZ];
  size_t n = bnb_build_signed_query("q=1", "sec", 1000000000000LL,
                                    out, sizeof(out));
  assert(n > 0);
  const char *sig_start = strstr(out, "signature=");
  assert(sig_start != NULL);
  sig_start += strlen("signature=");
  // Count hex chars until NUL or non-hex.
  int hex_len = 0;
  while(*sig_start && ((*sig_start >= '0' && *sig_start <= '9') ||
                        (*sig_start >= 'a' && *sig_start <= 'f')))
  {
    hex_len++;
    sig_start++;
  }
  assert(hex_len == 64);
}

// Deterministic: same inputs, same now_ms, same output.
static void
test_signed_query_is_deterministic(void)
{
  char out1[BNB_SIGNED_QS_SZ];
  char out2[BNB_SIGNED_QS_SZ];
  size_t n1 = bnb_build_signed_query("a=b", "mykey", 1700000000000LL,
                                     out1, sizeof(out1));
  size_t n2 = bnb_build_signed_query("a=b", "mykey", 1700000000000LL,
                                     out2, sizeof(out2));
  assert(n1 > 0 && n2 > 0);
  assert(n1 == n2);
  assert(strcmp(out1, out2) == 0);
}

// Different secrets produce different signatures.
static void
test_signed_query_different_secrets_differ(void)
{
  char out1[BNB_SIGNED_QS_SZ];
  char out2[BNB_SIGNED_QS_SZ];
  bnb_build_signed_query("a=b", "secret1", 1700000000000LL, out1, sizeof(out1));
  bnb_build_signed_query("a=b", "secret2", 1700000000000LL, out2, sizeof(out2));
  assert(strcmp(out1, out2) != 0);
}

// Output buffer too small — must return 0.
static void
test_signed_query_rejects_small_output(void)
{
  char out[8];
  size_t n = bnb_build_signed_query("x=1", "sec", 0, out, sizeof(out));
  assert(n == 0);
}

// NULL inputs — must return 0.
static void
test_signed_query_rejects_null_inputs(void)
{
  char out[BNB_SIGNED_QS_SZ];
  assert(bnb_build_signed_query(NULL, "sec", 0, out, sizeof(out)) == 0);
  assert(bnb_build_signed_query("x=1", NULL, 0, out, sizeof(out)) == 0);
  assert(bnb_build_signed_query("x=1", "sec", 0, NULL, sizeof(out)) == 0);
}

// now_ms=0 falls back to wall clock — result must still contain a timestamp.
static void
test_signed_query_nowms_zero_uses_wallclock(void)
{
  char out[BNB_SIGNED_QS_SZ];
  size_t n = bnb_build_signed_query("x=1", "sec", 0, out, sizeof(out));
  assert(n > 0);
  assert(strstr(out, "timestamp=") != NULL);
  // Timestamp should be > 2024-01-01 epoch (sanity check, not brittle).
  const char *ts = strstr(out, "timestamp=") + strlen("timestamp=");
  long long ts_ms = 0;
  sscanf(ts, "%lld", &ts_ms);
  assert(ts_ms > 1700000000000LL);
}

// -----------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------

int
main(void)
{
  test_hmac_sha256_rfc_vector();
  test_hmac_sha256_empty_message();
  test_hmac_sha256_rejects_small_output();
  test_hmac_sha256_rejects_null_inputs();
  test_hmac_sha256_output_length();
  test_hmac_sha256_output_is_lowercase_hex();

  test_signed_query_contains_required_fields();
  test_signed_query_signature_is_64_hex_chars();
  test_signed_query_is_deterministic();
  test_signed_query_different_secrets_differ();
  test_signed_query_rejects_small_output();
  test_signed_query_rejects_null_inputs();
  test_signed_query_nowms_zero_uses_wallclock();

  puts("All trading signing tests passed.");
  return(0);
}
