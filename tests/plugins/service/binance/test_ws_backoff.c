#define BNB_INTERNAL
#include "../../../../plugins/service/binance/binance.h"

#include <assert.h>
#include <stdio.h>

static void
test_reconnect_backoff_caps_exponential_growth(void)
{
  uint32_t out = 0;

  assert(bnb_ws_reconnect_backoff_ms(1000, 0, 60000, &out));
  assert(out == 1000);
  assert(bnb_ws_reconnect_backoff_ms(1000, 1, 60000, &out));
  assert(out == 2000);
  assert(bnb_ws_reconnect_backoff_ms(1000, 5, 60000, &out));
  assert(out == 32000);
  assert(bnb_ws_reconnect_backoff_ms(1000, 6, 60000, &out));
  assert(out == 60000);
  assert(bnb_ws_reconnect_backoff_ms(1000, 99, 60000, &out));
  assert(out == 60000);
}

static void
test_reconnect_backoff_validates_inputs(void)
{
  uint32_t out = 0;

  assert(!bnb_ws_reconnect_backoff_ms(0, 0, 60000, &out));
  assert(!bnb_ws_reconnect_backoff_ms(1000, 0, 0, &out));
  assert(!bnb_ws_reconnect_backoff_ms(1000, 0, 60000, NULL));
  assert(bnb_ws_reconnect_backoff_ms(1000, 0, 500, &out));
  assert(out == 500);
}

int
main(void)
{
  test_reconnect_backoff_caps_exponential_growth();
  test_reconnect_backoff_validates_inputs();
  puts("test_ws_backoff: ok");
  return(0);
}
