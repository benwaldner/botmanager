#define BNB_INTERNAL
#include "../../../../plugins/service/binance/binance.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static bnb_bar_t
make_bar(const char *symbol, double close, int64_t open_time_ms)
{
  bnb_bar_t bar;
  memset(&bar, 0, sizeof(bar));
  snprintf(bar.symbol, sizeof(bar.symbol), "%s", symbol);
  bar.open_time_ms = open_time_ms;
  bar.close_time_ms = open_time_ms + BNB_BAR_SECONDS * 1000 - 1;
  bar.open = close - 1.0;
  bar.high = close + 1.0;
  bar.low = close - 2.0;
  bar.close = close;
  bar.volume_base = 100.0;
  bar.volume_quote = 100.0 * close;
  bar.trade_count = 12;
  bar.finalized = true;
  return(bar);
}

static void
test_insert_get_and_update(void)
{
  bnb_bar_cache_t cache;
  bnb_bar_t sol = make_bar("SOLUSDT", 86.25, 1777429800000);
  bnb_bar_t updated = make_bar("SOLUSDT", 87.50, 1777430100000);
  bnb_bar_t out;

  bnb_bar_cache_init(&cache);
  assert(bnb_bar_cache_count(&cache) == 0);
  assert(bnb_bar_cache_upsert(&cache, &sol));
  assert(bnb_bar_cache_count(&cache) == 1);
  assert(bnb_bar_cache_get(&cache, "solusdt", &out));
  assert(out.close == 86.25);
  assert(bnb_bar_cache_upsert(&cache, &updated));
  assert(bnb_bar_cache_count(&cache) == 1);
  assert(bnb_bar_cache_get(&cache, "SOLUSDT", &out));
  assert(out.close == 87.50);
  assert(out.open_time_ms == 1777430100000);
  assert(!bnb_bar_cache_get(&cache, "ETHUSDT", &out));
  bnb_bar_cache_destroy(&cache);
}

static void
test_rejects_empty_symbol(void)
{
  bnb_bar_cache_t cache;
  bnb_bar_t empty = make_bar("", 1.0, 1);

  bnb_bar_cache_init(&cache);
  assert(!bnb_bar_cache_upsert(&cache, &empty));
  assert(bnb_bar_cache_count(&cache) == 0);
  bnb_bar_cache_destroy(&cache);
}

int
main(void)
{
  test_insert_get_and_update();
  test_rejects_empty_symbol();
  puts("test_bar_cache: ok");
  return(0);
}
