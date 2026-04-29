#define BNB_INTERNAL
#include "../../../../plugins/service/binance/binance.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
build_frame(char *out, size_t out_sz, const char *symbol, const char *interval,
    bool finalized, double close)
{
  snprintf(out, out_sz,
      "{"
      "\"stream\":\"%s@kline_%s\","
      "\"data\":{"
        "\"e\":\"kline\","
        "\"s\":\"%s\","
        "\"k\":{"
          "\"t\":1777429800000,"
          "\"T\":1777430099999,"
          "\"s\":\"%s\","
          "\"i\":\"%s\","
          "\"o\":\"86.10000000\","
          "\"c\":\"%.8f\","
          "\"h\":\"86.40000000\","
          "\"l\":\"86.05000000\","
          "\"v\":\"1234.50000000\","
          "\"n\":42,"
          "\"x\":%s,"
          "\"q\":\"106481.25000000\""
        "}"
      "}"
      "}",
      symbol, interval, symbol, symbol, interval, close,
      finalized ? "true" : "false");
}

static void
test_partial_updates_subscription_only(void)
{
  bnb_subscription_table_t table;
  bnb_bar_cache_t cache;
  bnb_bar_t applied;
  bnb_bar_t current;
  char frame[1024];

  bnb_subscription_table_init(&table);
  bnb_bar_cache_init(&cache);
  assert(bnb_subscription_table_add(&table, "SOLUSDT"));

  build_frame(frame, sizeof(frame), "SOLUSDT", "5m", false, 86.25);
  assert(bnb_market_data_apply_kline_frame(&table, &cache, frame, "5m", &applied));
  assert(strcmp(applied.symbol, "SOLUSDT") == 0);
  assert(!applied.finalized);
  assert(bnb_subscription_table_get_bar(&table, "solusdt", &current));
  assert(current.close == 86.25);
  assert(bnb_bar_cache_count(&cache) == 0);

  bnb_bar_cache_destroy(&cache);
  bnb_subscription_table_destroy(&table);
}

static void
test_finalized_updates_cache(void)
{
  bnb_subscription_table_t table;
  bnb_bar_cache_t cache;
  bnb_bar_t cached;
  char frame[1024];

  bnb_subscription_table_init(&table);
  bnb_bar_cache_init(&cache);
  assert(bnb_subscription_table_add(&table, "SOLUSDT"));

  build_frame(frame, sizeof(frame), "SOLUSDT", "5m", true, 87.50);
  assert(bnb_market_data_apply_kline_frame(&table, &cache, frame, "5m", NULL));
  assert(bnb_bar_cache_count(&cache) == 1);
  assert(bnb_bar_cache_get(&cache, "SOLUSDT", &cached));
  assert(cached.finalized);
  assert(cached.close == 87.50);

  bnb_bar_cache_destroy(&cache);
  bnb_subscription_table_destroy(&table);
}

static void
test_rejects_unsubscribed_or_wrong_interval(void)
{
  bnb_subscription_table_t table;
  bnb_bar_cache_t cache;
  char frame[1024];

  bnb_subscription_table_init(&table);
  bnb_bar_cache_init(&cache);
  assert(bnb_subscription_table_add(&table, "SOLUSDT"));

  build_frame(frame, sizeof(frame), "ETHUSDT", "5m", true, 1.0);
  assert(!bnb_market_data_apply_kline_frame(&table, &cache, frame, "5m", NULL));

  build_frame(frame, sizeof(frame), "SOLUSDT", "1m", true, 1.0);
  assert(!bnb_market_data_apply_kline_frame(&table, &cache, frame, "5m", NULL));
  assert(bnb_bar_cache_count(&cache) == 0);

  bnb_bar_cache_destroy(&cache);
  bnb_subscription_table_destroy(&table);
}

int
main(void)
{
  test_partial_updates_subscription_only();
  test_finalized_updates_cache();
  test_rejects_unsubscribed_or_wrong_interval();
  puts("test_market_data_dispatch: ok");
  return(0);
}
