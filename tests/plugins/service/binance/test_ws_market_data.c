#define BNB_INTERNAL
#include "../../../../plugins/service/binance/binance.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void
test_parse_combined_kline_frame(void)
{
  const char *frame =
    "{"
    "\"stream\":\"solusdt@kline_5m\","
    "\"data\":{"
      "\"e\":\"kline\","
      "\"E\":1777430000000,"
      "\"s\":\"SOLUSDT\","
      "\"k\":{"
        "\"t\":1777429800000,"
        "\"T\":1777430099999,"
        "\"s\":\"SOLUSDT\","
        "\"i\":\"5m\","
        "\"o\":\"86.10000000\","
        "\"c\":\"86.25000000\","
        "\"h\":\"86.40000000\","
        "\"l\":\"86.05000000\","
        "\"v\":\"1234.50000000\","
        "\"n\":42,"
        "\"x\":true,"
        "\"q\":\"106481.25000000\""
      "}"
    "}"
    "}";
  bnb_bar_t bar;
  char interval[BNB_INTERVAL_SZ];

  assert(bnb_ws_parse_kline_frame(frame, &bar, interval, sizeof(interval)));
  assert(strcmp(bar.symbol, "SOLUSDT") == 0);
  assert(strcmp(interval, "5m") == 0);
  assert(bar.open_time_ms == 1777429800000);
  assert(bar.close_time_ms == 1777430099999);
  assert(fabs(bar.open - 86.10) < 0.000001);
  assert(fabs(bar.close - 86.25) < 0.000001);
  assert(fabs(bar.high - 86.40) < 0.000001);
  assert(fabs(bar.low - 86.05) < 0.000001);
  assert(fabs(bar.volume_base - 1234.5) < 0.000001);
  assert(fabs(bar.volume_quote - 106481.25) < 0.000001);
  assert(bar.trade_count == 42);
  assert(bar.finalized == true);
}

static void
test_rejects_non_kline_frame(void)
{
  bnb_bar_t bar;
  assert(!bnb_ws_parse_kline_frame("{\"data\":{\"e\":\"trade\"}}", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("not-json", &bar, NULL, 0));
}

static void
test_parse_control_response(void)
{
  bnb_ws_control_response_t response;

  assert(bnb_ws_parse_control_response("{\"result\":null,\"id\":7}", &response));
  assert(response.kind == BNB_WS_CONTROL_ACK);
  assert(response.request_id == 7);
  assert(response.code == 0);
  assert(response.result_count == 0);

  assert(bnb_ws_parse_control_response(
        "{\"result\":[\"btcusdt@kline_5m\",\"ethusdt@kline_5m\"],\"id\":8}",
        &response));
  assert(response.kind == BNB_WS_CONTROL_RESULT_LIST);
  assert(response.request_id == 8);
  assert(response.result_count == 2);

  assert(bnb_ws_parse_control_response(
        "{\"code\":2,\"msg\":\"Invalid request\",\"id\":9}", &response));
  assert(response.kind == BNB_WS_CONTROL_ERROR);
  assert(response.request_id == 9);
  assert(response.code == 2);
  assert(strcmp(response.msg, "Invalid request") == 0);

  assert(bnb_ws_parse_control_response(
        "{\"error\":{\"code\":3,\"msg\":\"Bad payload\"},\"id\":10}", &response));
  assert(response.kind == BNB_WS_CONTROL_ERROR);
  assert(response.request_id == 10);
  assert(response.code == 3);
  assert(strcmp(response.msg, "Bad payload") == 0);

  assert(!bnb_ws_parse_control_response("{\"data\":{\"e\":\"kline\"}}", &response));
  assert(!bnb_ws_parse_control_response("not-json", &response));
}

static void
test_build_stream_and_subscribe_payload(void)
{
  const char *symbols[] = { "SOLUSDT", "btcusdt" };
  const char *sparse_symbols[] = { NULL, "ETHUSDT" };
  char stream[BNB_STREAM_SZ];
  char url[BNB_URL_SZ];
  char tiny_url[24];
  char payload[BNB_WS_PAYLOAD_SZ];

  assert(bnb_ws_build_stream_name("SOLUSDT", "5m", stream, sizeof(stream)));
  assert(strcmp(stream, "solusdt@kline_5m") == 0);

  assert(bnb_ws_build_combined_stream_url(BNB_WS_BASE, symbols, 2, "5m",
        url, sizeof(url)));
  assert(strstr(url, BNB_WS_BASE "?streams=") == url);
  assert(strstr(url, "solusdt@kline_5m/btcusdt@kline_5m") != NULL);

  assert(bnb_ws_build_combined_stream_url(BNB_WS_BASE, sparse_symbols, 2, "1h",
        url, sizeof(url)));
  assert(strstr(url, "?streams=/") == NULL);
  assert(strstr(url, "ethusdt@kline_1h") != NULL);
  assert(!bnb_ws_build_combined_stream_url(BNB_WS_BASE, sparse_symbols, 1, "1h",
        url, sizeof(url)));
  assert(!bnb_ws_build_combined_stream_url(BNB_WS_BASE, symbols, 2, "5m",
        tiny_url, sizeof(tiny_url)));

  assert(bnb_ws_build_subscribe_payload(symbols, 2, "5m", 7, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"SUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"solusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"id\":7") != NULL);

  assert(bnb_ws_build_unsubscribe_payload(symbols, 2, "15m", 8, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"UNSUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"solusdt@kline_15m\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_15m\"") != NULL);
  assert(strstr(payload, "\"id\":8") != NULL);

  assert(bnb_ws_build_subscribe_payload(sparse_symbols, 2, "1h", 9, payload, sizeof(payload)));
  assert(strstr(payload, "[,\"") == NULL);
  assert(strstr(payload, "\"ethusdt@kline_1h\"") != NULL);

  assert(bnb_ws_build_list_subscriptions_payload(10, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"LIST_SUBSCRIPTIONS\"") != NULL);
  assert(strstr(payload, "\"id\":10") != NULL);
  assert(!bnb_ws_build_list_subscriptions_payload(10, NULL, sizeof(payload)));
  assert(!bnb_ws_build_list_subscriptions_payload(10, payload, 0));
}

static void
test_interval_from_bar_seconds(void)
{
  char interval[BNB_INTERVAL_SZ];

  assert(bnb_interval_from_bar_seconds(60, interval, sizeof(interval)));
  assert(strcmp(interval, "1m") == 0);
  assert(bnb_interval_from_bar_seconds(300, interval, sizeof(interval)));
  assert(strcmp(interval, "5m") == 0);
  assert(bnb_interval_from_bar_seconds(900, interval, sizeof(interval)));
  assert(strcmp(interval, "15m") == 0);
  assert(bnb_interval_from_bar_seconds(3600, interval, sizeof(interval)));
  assert(strcmp(interval, "1h") == 0);
  assert(bnb_interval_from_bar_seconds(14400, interval, sizeof(interval)));
  assert(strcmp(interval, "4h") == 0);
  assert(bnb_interval_from_bar_seconds(86400, interval, sizeof(interval)));
  assert(strcmp(interval, "1d") == 0);
  assert(!bnb_interval_from_bar_seconds(120, interval, sizeof(interval)));
  assert(interval[0] == '\0');
}

int
main(void)
{
  test_parse_combined_kline_frame();
  test_rejects_non_kline_frame();
  test_parse_control_response();
  test_build_stream_and_subscribe_payload();
  test_interval_from_bar_seconds();
  puts("test_ws_market_data: ok");
  return(0);
}
