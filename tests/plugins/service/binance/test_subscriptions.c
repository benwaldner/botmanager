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
  char url[BNB_URL_SZ];
  char tiny_url[24];

  bnb_subscription_table_init(&table);
  assert(!bnb_subscription_table_build_subscribe_payload(
        &table, "5m", 11, payload, sizeof(payload)));
  assert(!bnb_subscription_table_build_unsubscribe_payload(
        &table, "5m", 11, payload, sizeof(payload)));
  assert(!bnb_subscription_table_build_combined_stream_url(
        &table, BNB_WS_BASE, "5m", url, sizeof(url)));

  assert(bnb_subscription_table_add(&table, "BTCUSDT"));
  assert(bnb_subscription_table_add(&table, "ethusdt"));
  assert(bnb_subscription_table_build_subscribe_payload(
        &table, "5m", 11, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"SUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"ethusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"id\":11") != NULL);

  assert(bnb_subscription_table_build_unsubscribe_payload(
        &table, "15m", 13, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"UNSUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_15m\"") != NULL);
  assert(strstr(payload, "\"ethusdt@kline_15m\"") != NULL);
  assert(strstr(payload, "\"id\":13") != NULL);

  assert(bnb_subscription_table_build_combined_stream_url(
        &table, BNB_WS_BASE, "1h", url, sizeof(url)));
  assert(strstr(url, BNB_WS_BASE "?streams=") == url);
  assert(strstr(url, "btcusdt@kline_1h/ethusdt@kline_1h") != NULL);
  assert(!bnb_subscription_table_build_combined_stream_url(
        &table, BNB_WS_BASE, "1h", tiny_url, sizeof(tiny_url)));

  bnb_subscription_table_destroy(&table);
}

static void
test_build_connection_plan(void)
{
  bnb_subscription_table_t table;
  bnb_ws_connection_plan_t plan;

  bnb_subscription_table_init(&table);
  assert(!bnb_ws_build_connection_plan(&table, BNB_WS_BASE, 300, 77, &plan));

  assert(bnb_subscription_table_add(&table, "BTCUSDT"));
  assert(bnb_subscription_table_add(&table, "ETHUSDT"));
  assert(!bnb_ws_build_connection_plan(&table, BNB_WS_BASE, 120, 77, &plan));
  assert(bnb_ws_build_connection_plan(&table, BNB_WS_BASE, 300, 77, &plan));

  assert(strcmp(plan.interval, "5m") == 0);
  assert(plan.symbol_count == 2);
  assert(plan.request_id == 77);
  assert(strstr(plan.url, BNB_WS_BASE "?streams=") == plan.url);
  assert(strstr(plan.url, "btcusdt@kline_5m/ethusdt@kline_5m") != NULL);
  assert(strstr(plan.subscribe_payload, "\"method\":\"SUBSCRIBE\"") != NULL);
  assert(strstr(plan.subscribe_payload, "\"btcusdt@kline_5m\"") != NULL);
  assert(strstr(plan.subscribe_payload, "\"ethusdt@kline_5m\"") != NULL);
  assert(strstr(plan.subscribe_payload, "\"id\":77") != NULL);

  bnb_subscription_table_destroy(&table);
}

static void
test_add_csv_normalizes_and_deduplicates(void)
{
  bnb_subscription_table_t table;
  char payload[BNB_WS_PAYLOAD_SZ];
  char symbols[BNB_MAX_SYMBOLS][BNB_SYMBOL_SZ];

  bnb_subscription_table_init(&table);
  assert(bnb_subscription_table_add_csv(&table,
        " BTCUSDT,ethusdt, BTCUSDT ,,bad/symbol,solusdt ") == 3);
  assert(bnb_subscription_table_count(&table) == 3);
  assert(bnb_subscription_table_contains(&table, "BTCUSDT"));
  assert(bnb_subscription_table_contains(&table, "ETHUSDT"));
  assert(bnb_subscription_table_contains(&table, "SOLUSDT"));
  assert(!bnb_subscription_table_contains(&table, "bad/symbol"));

  assert(bnb_subscription_table_build_subscribe_payload(
        &table, "5m", 12, payload, sizeof(payload)));
  assert(strstr(payload, "\"btcusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"ethusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"solusdt@kline_5m\"") != NULL);

  assert(bnb_subscription_table_snapshot(&table, symbols, BNB_MAX_SYMBOLS) == 3);
  assert(strcmp(symbols[0], "BTCUSDT") == 0);
  assert(strcmp(symbols[1], "ETHUSDT") == 0);
  assert(strcmp(symbols[2], "SOLUSDT") == 0);
  assert(bnb_subscription_table_snapshot(&table, symbols, 2) == 2);
  assert(bnb_subscription_table_snapshot(&table, symbols, 0) == 0);

  bnb_subscription_table_destroy(&table);
}

int
main(void)
{
  test_add_contains_remove();
  test_build_payload_snapshot();
  test_build_connection_plan();
  test_add_csv_normalizes_and_deduplicates();
  puts("test_subscriptions: ok");
  return(0);
}
