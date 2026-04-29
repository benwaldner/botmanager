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
  assert(!bnb_ws_parse_kline_frame("{\"data\":1}", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("{\"data\":null}", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("{\"data\":{\"e\":\"kline\",\"k\":1}}", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("{\"data\":{\"e\":\"kline\",\"k\":null}}", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("[]", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("\"kline\"", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("7", &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame("not-json", &bar, NULL, 0));
}

static void
test_rejects_mismatched_combined_stream_frame(void)
{
  const char *wrong_symbol =
    "{"
    "\"stream\":\"btcusdt@kline_5m\","
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *wrong_interval =
    "{"
    "\"stream\":\"solusdt@kline_1m\","
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(wrong_symbol, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(wrong_interval, &bar, NULL, 0));
}

static void
test_rejects_non_string_combined_stream_field(void)
{
  const char *frame =
    "{"
    "\"stream\":1,"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(frame, &bar, NULL, 0));
}

static void
test_rejects_mismatched_data_and_kline_symbol(void)
{
  const char *frame =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"ETHUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(frame, &bar, NULL, 0));
}

static void
test_rejects_non_string_kline_metadata(void)
{
  const char *numeric_event =
    "{"
    "\"data\":{\"e\":1,\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *numeric_data_symbol =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":1,"
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *numeric_kline_symbol =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":1,"
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *numeric_interval =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":5,\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *empty_kline_symbol =
    "{"
    "\"data\":{\"e\":\"kline\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *hyphen_kline_symbol =
    "{"
    "\"data\":{\"e\":\"kline\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOL-USDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *space_data_symbol =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOL USDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,"
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(numeric_event, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(numeric_data_symbol, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(numeric_kline_symbol, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(numeric_interval, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(empty_kline_symbol, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(hyphen_kline_symbol, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(space_data_symbol, &bar, NULL, 0));
}

static void
test_rejects_invalid_ohlc_kline_frame(void)
{
  const char *missing_open =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *bad_range =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"2\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *negative_volume =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"-1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *non_numeric_close =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"bad\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *nan_open =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"NaN\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *infinite_volume =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"inf\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *overflow_quote_volume =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1e9999\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(missing_open, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(bad_range, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(negative_volume, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(non_numeric_close, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(nan_open, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(infinite_volume, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(overflow_quote_volume, &bar, NULL, 0));
}

static void
test_rejects_invalid_kline_time_window(void)
{
  const char *missing_open_time =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *close_before_open =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777430099999,\"T\":1777429800000,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *same_open_close =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777429800000,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(missing_open_time, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(close_before_open, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(same_open_close, &bar, NULL, 0));
}

static void
test_rejects_non_integer_kline_timestamps(void)
{
  const char *string_open_time =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":\"1777429800000\",\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *double_close_time =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999.5,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *string_event_time =
    "{"
    "\"data\":{\"e\":\"kline\",\"E\":\"1777430000000\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *double_event_time =
    "{"
    "\"data\":{\"e\":\"kline\",\"E\":1777430000000.5,\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *negative_event_time =
    "{"
    "\"data\":{\"e\":\"kline\",\"E\":-1,\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(string_open_time, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(double_close_time, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(string_event_time, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(double_event_time, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(negative_event_time, &bar, NULL, 0));
}

static void
test_rejects_unsupported_kline_interval(void)
{
  const char *frame =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"2m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(!bnb_ws_parse_kline_frame(frame, &bar, NULL, 0));
}

static void
test_validates_kline_finalized_flag(void)
{
  const char *partial =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":false,\"q\":\"1\"}}"
    "}";
  const char *missing =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"q\":\"1\"}}"
    "}";
  const char *non_boolean =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":1,\"x\":\"false\",\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(bnb_ws_parse_kline_frame(partial, &bar, NULL, 0));
  assert(bar.finalized == false);
  assert(!bnb_ws_parse_kline_frame(missing, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(non_boolean, &bar, NULL, 0));
}

static void
test_validates_kline_trade_count(void)
{
  const char *zero_trades =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":0,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *missing =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *negative =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":-1,\"x\":true,\"q\":\"1\"}}"
    "}";
  const char *non_integer =
    "{"
    "\"data\":{\"e\":\"kline\",\"s\":\"SOLUSDT\","
      "\"k\":{\"t\":1777429800000,\"T\":1777430099999,\"s\":\"SOLUSDT\","
      "\"i\":\"5m\",\"o\":\"1\",\"c\":\"1\",\"h\":\"1\",\"l\":\"1\","
      "\"v\":\"1\",\"n\":\"1\",\"x\":true,\"q\":\"1\"}}"
    "}";
  bnb_bar_t bar;

  assert(bnb_ws_parse_kline_frame(zero_trades, &bar, NULL, 0));
  assert(bar.trade_count == 0);
  assert(!bnb_ws_parse_kline_frame(missing, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(negative, &bar, NULL, 0));
  assert(!bnb_ws_parse_kline_frame(non_integer, &bar, NULL, 0));
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

  assert(bnb_ws_parse_control_response("{\"result\":[],\"id\":12}", &response));
  assert(response.kind == BNB_WS_CONTROL_RESULT_LIST);
  assert(response.request_id == 12);
  assert(response.result_count == 0);

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

  assert(bnb_ws_parse_control_response(
        "{\"error\":{\"code\":-1003,\"msg\":\"Rate limited\"},\"id\":11}",
        &response));
  assert(response.kind == BNB_WS_CONTROL_ERROR);
  assert(response.request_id == 11);
  assert(response.code == -1003);
  assert(strcmp(response.msg, "Rate limited") == 0);

  assert(bnb_ws_parse_control_response("{\"code\":2,\"id\":13}", &response));
  assert(response.kind == BNB_WS_CONTROL_ERROR);
  assert(response.request_id == 13);
  assert(response.code == 2);
  assert(strcmp(response.msg, "") == 0);

  assert(!bnb_ws_parse_control_response(
        "{\"result\":null,\"id\":\"7\"}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":null,\"id\":-1}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":null,\"id\":7.5}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"code\":\"2\",\"msg\":\"Invalid request\",\"id\":9}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"code\":2.5,\"msg\":\"Invalid request\",\"id\":9}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"error\":{\"code\":\"3\",\"msg\":\"Bad payload\"},\"id\":10}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"error\":{\"code\":3.5,\"msg\":\"Bad payload\"},\"id\":10}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"error\":{\"msg\":\"Bad payload\"},\"id\":10}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"code\":2,\"msg\":7,\"id\":9}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"code\":2,\"msg\":{\"text\":\"Invalid request\"},\"id\":9}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"error\":{\"code\":3,\"msg\":7},\"id\":10}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"error\":{\"code\":3,\"msg\":{\"text\":\"Bad payload\"}},\"id\":10}",
        &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":[\"btcusdt@kline_5m\",1],\"id\":8}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":[\"btcusdt@kline_5m\",null],\"id\":8}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":[{\"stream\":\"btcusdt@kline_5m\"}],\"id\":8}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":[\"btcusdt@trade\"],\"id\":8}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":[\"btcusdt@kline_2m\"],\"id\":8}", &response));
  assert(!bnb_ws_parse_control_response(
        "{\"result\":[\"btc-usdt@kline_5m\"],\"id\":8}", &response));
  assert(!bnb_ws_parse_control_response("{\"data\":{\"e\":\"kline\"}}", &response));
  assert(!bnb_ws_parse_control_response("[]", &response));
  assert(!bnb_ws_parse_control_response("\"result\"", &response));
  assert(!bnb_ws_parse_control_response("7", &response));
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
  assert(!bnb_ws_build_stream_name("SOL-USDT", "5m", stream, sizeof(stream)));
  assert(stream[0] == '\0');
  snprintf(stream, sizeof(stream), "%s", "stale");
  assert(!bnb_ws_build_stream_name("SOL USDT", "5m", stream, sizeof(stream)));
  assert(stream[0] == '\0');
  snprintf(stream, sizeof(stream), "%s", "stale");
  assert(!bnb_ws_build_stream_name("SOLUSDT", "2m", stream, sizeof(stream)));
  assert(stream[0] == '\0');
  snprintf(stream, sizeof(stream), "%s", "stale");
  assert(!bnb_ws_build_stream_name("SOLUSDT", "5m", stream, 4));
  assert(stream[0] == '\0');

  assert(bnb_ws_build_combined_stream_url(BNB_WS_BASE, symbols, 2, "5m",
        url, sizeof(url)));
  assert(strstr(url, BNB_WS_BASE "?streams=") == url);
  assert(strstr(url, "solusdt@kline_5m/btcusdt@kline_5m") != NULL);

  assert(bnb_ws_build_combined_stream_url(BNB_WS_BASE, sparse_symbols, 2, "1h",
        url, sizeof(url)));
  assert(strstr(url, "?streams=/") == NULL);
  assert(strstr(url, "ethusdt@kline_1h") != NULL);
  snprintf(url, sizeof(url), "%s", "stale");
  assert(!bnb_ws_build_combined_stream_url(BNB_WS_BASE, sparse_symbols, 1, "1h",
        url, sizeof(url)));
  assert(url[0] == '\0');
  snprintf(url, sizeof(url), "%s", "stale");
  assert(!bnb_ws_build_combined_stream_url(BNB_WS_BASE, symbols, 2, "2m",
        url, sizeof(url)));
  assert(url[0] == '\0');
  sparse_symbols[0] = "BAD-SYMBOL";
  snprintf(url, sizeof(url), "%s", "stale");
  assert(!bnb_ws_build_combined_stream_url(BNB_WS_BASE, sparse_symbols, 2, "1h",
        url, sizeof(url)));
  assert(url[0] == '\0');
  sparse_symbols[0] = NULL;
  snprintf(tiny_url, sizeof(tiny_url), "%s", "stale");
  assert(!bnb_ws_build_combined_stream_url(BNB_WS_BASE, symbols, 2, "5m",
        tiny_url, sizeof(tiny_url)));
  assert(tiny_url[0] == '\0');

  assert(bnb_ws_build_subscribe_payload(symbols, 2, "5m", 7, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"SUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"solusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_5m\"") != NULL);
  assert(strstr(payload, "\"id\":7") != NULL);
  snprintf(payload, sizeof(payload), "%s", "stale");
  assert(!bnb_ws_build_subscribe_payload(symbols, 2, "2m", 7,
        payload, sizeof(payload)));
  assert(payload[0] == '\0');
  sparse_symbols[0] = "BAD SYMBOL";
  snprintf(payload, sizeof(payload), "%s", "stale");
  assert(!bnb_ws_build_subscribe_payload(sparse_symbols, 2, "1h", 9,
        payload, sizeof(payload)));
  assert(payload[0] == '\0');
  sparse_symbols[0] = NULL;

  assert(bnb_ws_build_unsubscribe_payload(symbols, 2, "15m", 8, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"UNSUBSCRIBE\"") != NULL);
  assert(strstr(payload, "\"solusdt@kline_15m\"") != NULL);
  assert(strstr(payload, "\"btcusdt@kline_15m\"") != NULL);
  assert(strstr(payload, "\"id\":8") != NULL);
  snprintf(payload, sizeof(payload), "%s", "stale");
  assert(!bnb_ws_build_unsubscribe_payload(symbols, 2, "2m", 8,
        payload, sizeof(payload)));
  assert(payload[0] == '\0');

  assert(bnb_ws_build_subscribe_payload(sparse_symbols, 2, "1h", 9, payload, sizeof(payload)));
  assert(strstr(payload, "[,\"") == NULL);
  assert(strstr(payload, "\"ethusdt@kline_1h\"") != NULL);

  assert(bnb_ws_build_list_subscriptions_payload(10, payload, sizeof(payload)));
  assert(strstr(payload, "\"method\":\"LIST_SUBSCRIPTIONS\"") != NULL);
  assert(strstr(payload, "\"id\":10") != NULL);
  assert(!bnb_ws_build_list_subscriptions_payload(10, NULL, sizeof(payload)));
  assert(!bnb_ws_build_list_subscriptions_payload(10, payload, 0));
  snprintf(payload, sizeof(payload), "%s", "stale");
  assert(!bnb_ws_build_list_subscriptions_payload(10, payload, 8));
  assert(payload[0] == '\0');
}

static void
test_parse_stream_name(void)
{
  char symbol[BNB_SYMBOL_SZ];
  char interval[BNB_INTERVAL_SZ];
  char tiny_symbol[4];

  assert(bnb_ws_parse_stream_name("solusdt@kline_5m",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(strcmp(symbol, "SOLUSDT") == 0);
  assert(strcmp(interval, "5m") == 0);
  assert(bnb_ws_parse_stream_name("BTCUSDT@kline_1h",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(strcmp(symbol, "BTCUSDT") == 0);
  assert(strcmp(interval, "1h") == 0);

  assert(!bnb_ws_parse_stream_name("@kline_5m",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(!bnb_ws_parse_stream_name("bad/symbol@kline_5m",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(!bnb_ws_parse_stream_name("solusdt@ticker",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(!bnb_ws_parse_stream_name("solusdt@kline_",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(!bnb_ws_parse_stream_name("solusdt@kline_2m",
        symbol, sizeof(symbol), interval, sizeof(interval)));
  assert(!bnb_ws_parse_stream_name("solusdt@kline_5m",
        tiny_symbol, sizeof(tiny_symbol), interval, sizeof(interval)));
}

static void
test_interval_from_bar_seconds(void)
{
  char interval[BNB_INTERVAL_SZ];

  assert(bnb_interval_from_bar_seconds(60, interval, sizeof(interval)));
  assert(strcmp(interval, "1m") == 0);
  assert(bnb_interval_from_bar_seconds(180, interval, sizeof(interval)));
  assert(strcmp(interval, "3m") == 0);
  assert(bnb_interval_from_bar_seconds(300, interval, sizeof(interval)));
  assert(strcmp(interval, "5m") == 0);
  assert(bnb_interval_from_bar_seconds(900, interval, sizeof(interval)));
  assert(strcmp(interval, "15m") == 0);
  assert(bnb_interval_from_bar_seconds(1800, interval, sizeof(interval)));
  assert(strcmp(interval, "30m") == 0);
  assert(bnb_interval_from_bar_seconds(3600, interval, sizeof(interval)));
  assert(strcmp(interval, "1h") == 0);
  assert(bnb_interval_from_bar_seconds(7200, interval, sizeof(interval)));
  assert(strcmp(interval, "2h") == 0);
  assert(bnb_interval_from_bar_seconds(14400, interval, sizeof(interval)));
  assert(strcmp(interval, "4h") == 0);
  assert(bnb_interval_from_bar_seconds(21600, interval, sizeof(interval)));
  assert(strcmp(interval, "6h") == 0);
  assert(bnb_interval_from_bar_seconds(28800, interval, sizeof(interval)));
  assert(strcmp(interval, "8h") == 0);
  assert(bnb_interval_from_bar_seconds(43200, interval, sizeof(interval)));
  assert(strcmp(interval, "12h") == 0);
  assert(bnb_interval_from_bar_seconds(86400, interval, sizeof(interval)));
  assert(strcmp(interval, "1d") == 0);
  assert(bnb_interval_from_bar_seconds(259200, interval, sizeof(interval)));
  assert(strcmp(interval, "3d") == 0);
  assert(bnb_interval_from_bar_seconds(604800, interval, sizeof(interval)));
  assert(strcmp(interval, "1w") == 0);
  assert(!bnb_interval_from_bar_seconds(120, interval, sizeof(interval)));
  assert(interval[0] == '\0');
}

int
main(void)
{
  test_parse_combined_kline_frame();
  test_rejects_non_kline_frame();
  test_rejects_mismatched_combined_stream_frame();
  test_rejects_non_string_combined_stream_field();
  test_rejects_mismatched_data_and_kline_symbol();
  test_rejects_non_string_kline_metadata();
  test_rejects_invalid_ohlc_kline_frame();
  test_rejects_invalid_kline_time_window();
  test_rejects_non_integer_kline_timestamps();
  test_rejects_unsupported_kline_interval();
  test_validates_kline_finalized_flag();
  test_validates_kline_trade_count();
  test_parse_control_response();
  test_build_stream_and_subscribe_payload();
  test_parse_stream_name();
  test_interval_from_bar_seconds();
  puts("test_ws_market_data: ok");
  return(0);
}
