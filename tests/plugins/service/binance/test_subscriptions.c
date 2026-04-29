#define BNB_INTERNAL
#include "../../../../plugins/service/binance/binance.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void
test_add_contains_remove(void)
{
  bnb_subscription_table_t table;

  bnb_subscription_table_init(&table);
  assert(bnb_subscription_table_count(&table) == 0);

  assert(bnb_subscription_table_add(&table, "solusdt"));
  assert(bnb_subscription_table_count(&table) == 1);
  assert(bnb_subscription_table_contains(&table, "SOLUSDT"));
  assert(bnb_subscription_table_contains(&table, "solusdt"));

  assert(bnb_subscription_table_add(&table, "SOLUSDT"));
  assert(bnb_subscription_table_count(&table) == 1);

  assert(!bnb_subscription_table_add(&table, ""));
  assert(!bnb_subscription_table_add(&table, "BAD/SYMBOL"));
  assert(bnb_subscription_table_count(&table) == 1);

  assert(bnb_subscription_table_remove(&table, "solusdt"));
  assert(bnb_subscription_table_count(&table) == 0);
  assert(!bnb_subscription_table_contains(&table, "SOLUSDT"));
  assert(!bnb_subscription_table_remove(&table, "SOLUSDT"));

  bnb_subscription_table_destroy(&table);
}

static void
test_build_payload_snapshot(void)
{
  bnb_subscription_table_t table;
  char payload[BNB_WS_PAYLOAD_SZ];

  bnb_subscription_table_init(&table);
  assert(!bnb_subscription_table_build_subscribe_payload(
        &table, "5m", 11, payload, sizeof(payload)));

  assert(bnb_subscription_table_add(&table, "BTCUSDT"));
  assert(bnb_subscription_table_add(&table, "ethusdt"));
  assert(bnb_subscription_table_build_subscribe_payload(
        &table, "5m", 11, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"SUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"ethusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"id\":11") != NULL);

  bnb_subscription_table_destroy(&table);
}

int
main(void)
{
  test_add_contains_remove();
  test_build_payload_snapshot();
  puts("test_subscriptions: ok");
  return(0);
}
