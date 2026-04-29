// =======================================================================
// Binance public market-data WebSocket helpers.
//
// This file is intentionally market-data-only. It parses public kline
// frames and builds public subscription payloads. It does not implement
// REST, signed endpoints, account state, or order placement.
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

#include <ctype.h>
#include <json-c/json.h>
#include <stdio.h>

typedef struct
{
  uint32_t seconds;
  const char *interval;
} bnb_interval_map_t;

static const bnb_interval_map_t bnb_supported_intervals[] = {
  { 60, "1m" },
  { 180, "3m" },
  { 300, "5m" },
  { 900, "15m" },
  { 1800, "30m" },
  { 3600, "1h" },
  { 7200, "2h" },
  { 14400, "4h" },
  { 21600, "6h" },
  { 28800, "8h" },
  { 43200, "12h" },
  { 86400, "1d" },
  { 259200, "3d" },
  { 604800, "1w" },
};

static bool
bnb_json_get_obj(struct json_object *obj, const char *key, struct json_object **out)
{
  if(obj == NULL || key == NULL || out == NULL)
    return(false);
  if(!json_object_object_get_ex(obj, key, out) || *out == NULL)
    return(false);
  return(true);
}

static const char *
bnb_json_get_str(struct json_object *obj, const char *key)
{
  struct json_object *value = NULL;
  if(!bnb_json_get_obj(obj, key, &value))
    return(NULL);
  if(json_object_get_type(value) != json_type_string)
    return(NULL);
  return(json_object_get_string(value));
}

static bool
bnb_json_get_optional_str(struct json_object *obj, const char *key,
    const char **out)
{
  struct json_object *value = NULL;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);

  *out = NULL;
  if(!json_object_object_get_ex(obj, key, &value))
    return(true);
  if(value == NULL || json_object_get_type(value) != json_type_string)
    return(false);

  *out = json_object_get_string(value);
  return(true);
}

static bool
bnb_json_get_required_u32(struct json_object *obj, const char *key,
    uint32_t *out)
{
  struct json_object *value = NULL;
  int64_t raw;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);
  if(!bnb_json_get_obj(obj, key, &value))
    return(false);
  if(json_object_get_type(value) != json_type_int)
    return(false);

  raw = json_object_get_int64(value);
  if(raw < 0 || raw > UINT32_MAX)
    return(false);

  *out = (uint32_t)raw;
  return(true);
}

static bool
bnb_json_get_required_i32(struct json_object *obj, const char *key,
    int32_t *out)
{
  struct json_object *value = NULL;
  int64_t raw;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);
  if(!bnb_json_get_obj(obj, key, &value))
    return(false);
  if(json_object_get_type(value) != json_type_int)
    return(false);

  raw = json_object_get_int64(value);
  if(raw < INT32_MIN || raw > INT32_MAX)
    return(false);

  *out = (int32_t)raw;
  return(true);
}

static bool
bnb_json_get_required_i64(struct json_object *obj, const char *key,
    int64_t *out)
{
  struct json_object *value = NULL;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);
  if(!bnb_json_get_obj(obj, key, &value))
    return(false);
  if(json_object_get_type(value) != json_type_int)
    return(false);

  *out = json_object_get_int64(value);
  return(true);
}

static bool
bnb_json_get_optional_u32(struct json_object *obj, const char *key,
    uint32_t *out)
{
  struct json_object *value = NULL;
  int64_t raw;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);

  *out = 0;
  if(!json_object_object_get_ex(obj, key, &value))
    return(true);
  if(value == NULL || json_object_get_type(value) != json_type_int)
    return(false);

  raw = json_object_get_int64(value);
  if(raw < 0 || raw > UINT32_MAX)
    return(false);

  *out = (uint32_t)raw;
  return(true);
}

static bool
bnb_json_get_required_double_str(struct json_object *obj, const char *key,
    double *out)
{
  const char *text;
  char *end = NULL;
  double value;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);

  text = bnb_json_get_str(obj, key);
  if(text == NULL || text[0] == '\0')
    return(false);

  value = strtod(text, &end);
  if(end == text || end == NULL || *end != '\0')
    return(false);

  *out = value;
  return(true);
}

static void
bnb_copy_upper(char *dst, size_t dst_sz, const char *src)
{
  size_t i;
  if(dst == NULL || dst_sz == 0)
    return;
  dst[0] = '\0';
  if(src == NULL)
    return;
  for(i = 0; i + 1 < dst_sz && src[i] != '\0'; i++)
    dst[i] = (char)toupper((unsigned char)src[i]);
  dst[i] = '\0';
}

static void
bnb_copy_lower(char *dst, size_t dst_sz, const char *src)
{
  size_t i;
  if(dst == NULL || dst_sz == 0)
    return;
  dst[0] = '\0';
  if(src == NULL)
    return;
  for(i = 0; i + 1 < dst_sz && src[i] != '\0'; i++)
    dst[i] = (char)tolower((unsigned char)src[i]);
  dst[i] = '\0';
}

static bool
bnb_interval_is_supported_str(const char *interval)
{
  size_t i;

  if(interval == NULL || interval[0] == '\0')
    return(false);

  for(i = 0; i < sizeof(bnb_supported_intervals) / sizeof(bnb_supported_intervals[0]); i++)
  {
    if(strcmp(interval, bnb_supported_intervals[i].interval) == 0)
      return(true);
  }

  return(false);
}

static bool
bnb_json_get_required_bool(struct json_object *obj, const char *key, bool *out)
{
  struct json_object *value = NULL;

  if(obj == NULL || key == NULL || out == NULL)
    return(false);
  if(!bnb_json_get_obj(obj, key, &value))
    return(false);
  if(json_object_get_type(value) != json_type_boolean)
    return(false);

  *out = json_object_get_boolean(value);
  return(true);
}

// Parse one Binance combined-stream kline frame into a finalized/partial bar.
// returns: true on successful parse, false on invalid or unsupported frame
bool
bnb_ws_parse_kline_frame(const char *frame, bnb_bar_t *out,
    char *interval, size_t interval_sz)
{
  struct json_object *root = NULL;
  struct json_object *data = NULL;
  struct json_object *kline = NULL;
  struct json_object *stream_obj = NULL;
  const char *event_type = NULL;
  const char *symbol = NULL;
  const char *kline_symbol = NULL;
  const char *kline_interval = NULL;
  char stream_symbol[BNB_SYMBOL_SZ];
  char stream_interval[BNB_INTERVAL_SZ];
  double open;
  double high;
  double low;
  double close;
  double volume_base;
  double volume_quote;
  uint32_t trade_count;
  int64_t open_time_ms;
  int64_t close_time_ms;
  bool finalized;

  if(frame == NULL || out == NULL)
    return(false);

  root = json_tokener_parse(frame);
  if(root == NULL)
    return(false);

  if(!bnb_json_get_obj(root, "data", &data))
    data = root;

  event_type = bnb_json_get_str(data, "e");
  if(event_type == NULL || strcmp(event_type, "kline") != 0)
  {
    json_object_put(root);
    return(false);
  }

  if(!bnb_json_get_obj(data, "k", &kline))
  {
    json_object_put(root);
    return(false);
  }

  if(!bnb_json_get_optional_str(data, "s", &symbol)
      || !bnb_json_get_optional_str(kline, "s", &kline_symbol)
      || !bnb_json_get_optional_str(kline, "i", &kline_interval))
  {
    json_object_put(root);
    return(false);
  }
  if(symbol != NULL && kline_symbol != NULL
      && strcasecmp(symbol, kline_symbol) != 0)
  {
    json_object_put(root);
    return(false);
  }
  if(kline_symbol != NULL)
    symbol = kline_symbol;
  if(symbol == NULL || kline_interval == NULL
      || !bnb_interval_is_supported_str(kline_interval))
  {
    json_object_put(root);
    return(false);
  }

  if(json_object_object_get_ex(root, "stream", &stream_obj))
  {
    if(stream_obj == NULL || json_object_get_type(stream_obj) != json_type_string)
    {
      json_object_put(root);
      return(false);
    }
    if(!bnb_ws_parse_stream_name(json_object_get_string(stream_obj),
          stream_symbol, sizeof(stream_symbol),
          stream_interval, sizeof(stream_interval))
        || strcasecmp(stream_symbol, symbol) != 0
        || strcasecmp(stream_interval, kline_interval) != 0)
    {
      json_object_put(root);
      return(false);
    }
  }

  if(!bnb_json_get_required_double_str(kline, "o", &open)
      || !bnb_json_get_required_double_str(kline, "h", &high)
      || !bnb_json_get_required_double_str(kline, "l", &low)
      || !bnb_json_get_required_double_str(kline, "c", &close)
      || !bnb_json_get_required_double_str(kline, "v", &volume_base)
      || !bnb_json_get_required_double_str(kline, "q", &volume_quote)
      || !bnb_json_get_required_u32(kline, "n", &trade_count)
      || !bnb_json_get_required_i64(kline, "t", &open_time_ms)
      || !bnb_json_get_required_i64(kline, "T", &close_time_ms)
      || !bnb_json_get_required_bool(kline, "x", &finalized))
  {
    json_object_put(root);
    return(false);
  }

  if(open < 0.0 || high < 0.0 || low < 0.0 || close < 0.0
      || volume_base < 0.0 || volume_quote < 0.0
      || high < low || open < low || open > high || close < low || close > high)
  {
    json_object_put(root);
    return(false);
  }

  memset(out, 0, sizeof(*out));
  bnb_copy_upper(out->symbol, sizeof(out->symbol), symbol);
  out->open_time_ms = open_time_ms;
  out->close_time_ms = close_time_ms;
  if(out->open_time_ms <= 0 || out->close_time_ms <= out->open_time_ms)
  {
    json_object_put(root);
    memset(out, 0, sizeof(*out));
    return(false);
  }

  out->open = open;
  out->high = high;
  out->low = low;
  out->close = close;
  out->volume_base = volume_base;
  out->volume_quote = volume_quote;
  out->trade_count = trade_count;
  out->finalized = finalized;

  if(interval != NULL && interval_sz > 0)
    snprintf(interval, interval_sz, "%s", kline_interval);

  json_object_put(root);
  return(out->symbol[0] != '\0');
}

// Parse a public WebSocket control response such as SUBSCRIBE ACK,
// UNSUBSCRIBE ACK, LIST_SUBSCRIPTIONS result, or an error object.
// returns: true if the frame is a recognized control response
bool
bnb_ws_parse_control_response(const char *frame, bnb_ws_control_response_t *out)
{
  struct json_object *root = NULL;
  struct json_object *value = NULL;
  struct json_object *error = NULL;
  const char *msg = NULL;
  size_t i;
  size_t result_count;

  if(frame == NULL || out == NULL)
    return(false);

  root = json_tokener_parse(frame);
  if(root == NULL)
    return(false);
  if(json_object_get_type(root) != json_type_object)
  {
    json_object_put(root);
    return(false);
  }

  memset(out, 0, sizeof(*out));
  out->kind = BNB_WS_CONTROL_UNKNOWN;

  if(!bnb_json_get_optional_u32(root, "id", &out->request_id))
  {
    json_object_put(root);
    return(false);
  }

  if(bnb_json_get_obj(root, "error", &error))
  {
    out->kind = BNB_WS_CONTROL_ERROR;
    if(!bnb_json_get_required_i32(error, "code", &out->code))
    {
      json_object_put(root);
      return(false);
    }
    if(!bnb_json_get_optional_str(error, "msg", &msg))
    {
      json_object_put(root);
      return(false);
    }
    if(msg != NULL)
      snprintf(out->msg, sizeof(out->msg), "%s", msg);
    json_object_put(root);
    return(true);
  }

  if(bnb_json_get_obj(root, "code", &value))
  {
    out->kind = BNB_WS_CONTROL_ERROR;
    if(!bnb_json_get_required_i32(root, "code", &out->code))
    {
      json_object_put(root);
      return(false);
    }
    if(!bnb_json_get_optional_str(root, "msg", &msg))
    {
      json_object_put(root);
      return(false);
    }
    if(msg != NULL)
      snprintf(out->msg, sizeof(out->msg), "%s", msg);
    json_object_put(root);
    return(true);
  }

  if(!json_object_object_get_ex(root, "result", &value))
  {
    json_object_put(root);
    return(false);
  }

  if(value == NULL || json_object_get_type(value) == json_type_null)
  {
    out->kind = BNB_WS_CONTROL_ACK;
    json_object_put(root);
    return(true);
  }

  if(json_object_get_type(value) == json_type_array)
  {
    result_count = json_object_array_length(value);
    if(result_count > UINT32_MAX)
    {
      json_object_put(root);
      return(false);
    }

    for(i = 0; i < result_count; i++)
    {
      struct json_object *entry = json_object_array_get_idx(value, i);
      char stream_symbol[BNB_SYMBOL_SZ];
      char stream_interval[BNB_INTERVAL_SZ];
      if(entry == NULL || json_object_get_type(entry) != json_type_string)
      {
        json_object_put(root);
        return(false);
      }
      if(!bnb_ws_parse_stream_name(json_object_get_string(entry), stream_symbol,
            sizeof(stream_symbol), stream_interval, sizeof(stream_interval)))
      {
        json_object_put(root);
        return(false);
      }
    }

    out->kind = BNB_WS_CONTROL_RESULT_LIST;
    out->result_count = (uint32_t)result_count;
    json_object_put(root);
    return(true);
  }

  json_object_put(root);
  return(false);
}

// Build one stream name such as "solusdt@kline_5m".
// returns: true if output fit
bool
bnb_ws_build_stream_name(const char *symbol, const char *interval,
    char *out, size_t out_sz)
{
  char lower_symbol[BNB_SYMBOL_SZ];
  int n;

  if(symbol == NULL || interval == NULL || out == NULL || out_sz == 0
      || !bnb_interval_is_supported_str(interval))
    return(false);

  bnb_copy_lower(lower_symbol, sizeof(lower_symbol), symbol);
  n = snprintf(out, out_sz, "%s@kline_%s", lower_symbol, interval);
  return(n > 0 && (size_t)n < out_sz);
}

// Parse one stream name such as "solusdt@kline_5m".
// returns: true if symbol/interval were extracted and fit outputs
bool
bnb_ws_parse_stream_name(const char *stream, char *symbol, size_t symbol_sz,
    char *interval, size_t interval_sz)
{
  const char *marker = "@kline_";
  const char *pos;
  const char *src_interval;
  size_t symbol_len;
  size_t i;
  int n;

  if(stream == NULL || symbol == NULL || symbol_sz == 0
      || interval == NULL || interval_sz == 0)
    return(false);

  symbol[0] = '\0';
  interval[0] = '\0';
  pos = strstr(stream, marker);
  if(pos == NULL || pos == stream)
    return(false);

  symbol_len = (size_t)(pos - stream);
  if(symbol_len + 1 > symbol_sz)
    return(false);
  for(i = 0; i < symbol_len; i++)
  {
    if(!isalnum((unsigned char)stream[i]))
      return(false);
    symbol[i] = (char)toupper((unsigned char)stream[i]);
  }
  symbol[symbol_len] = '\0';

  src_interval = pos + strlen(marker);
  if(src_interval[0] == '\0')
    return(false);
  for(i = 0; src_interval[i] != '\0'; i++)
  {
    if(!isalnum((unsigned char)src_interval[i]))
      return(false);
  }
  if(!bnb_interval_is_supported_str(src_interval))
    return(false);

  n = snprintf(interval, interval_sz, "%s", src_interval);
  return(n > 0 && (size_t)n < interval_sz);
}

// Build a Binance combined-stream URL such as:
// wss://.../stream?streams=solusdt@kline_5m/btcusdt@kline_5m
// returns: true if output fit and at least one symbol was included
bool
bnb_ws_build_combined_stream_url(const char *base_url,
    const char * const *symbols, uint32_t symbol_count, const char *interval,
    char *out, size_t out_sz)
{
  size_t used = 0;
  uint32_t i;
  uint32_t stream_count = 0;
  int n;

  if(base_url == NULL || symbols == NULL || symbol_count == 0 || interval == NULL
      || out == NULL || out_sz == 0)
    return(false);

  n = snprintf(out, out_sz, "%s?streams=", base_url);
  if(n < 0 || (size_t)n >= out_sz)
    return(false);
  used = (size_t)n;

  for(i = 0; i < symbol_count; i++)
  {
    char stream[BNB_STREAM_SZ];

    if(symbols[i] == NULL || symbols[i][0] == '\0')
      continue;
    if(!bnb_ws_build_stream_name(symbols[i], interval, stream, sizeof(stream)))
      return(false);

    n = snprintf(out + used, out_sz - used, "%s%s",
        stream_count == 0 ? "" : "/",
        stream);
    if(n < 0 || (size_t)n >= out_sz - used)
      return(false);
    used += (size_t)n;
    stream_count++;
  }

  return(stream_count > 0);
}

// Convert configured bar size to a Binance public kline interval.
// returns: true for supported intervals, false for unsupported values
bool
bnb_interval_from_bar_seconds(uint32_t bar_seconds, char *out, size_t out_sz)
{
  const char *interval = NULL;
  int n;
  size_t i;

  if(out == NULL || out_sz == 0)
    return(false);

  for(i = 0; i < sizeof(bnb_supported_intervals) / sizeof(bnb_supported_intervals[0]); i++)
  {
    if(bnb_supported_intervals[i].seconds == bar_seconds)
    {
      interval = bnb_supported_intervals[i].interval;
      break;
    }
  }
  if(interval == NULL)
  {
    out[0] = '\0';
    return(false);
  }

  n = snprintf(out, out_sz, "%s", interval);
  return(n > 0 && (size_t)n < out_sz);
}

static bool
bnb_ws_build_stream_method_payload(const char *method, const char * const *symbols,
    uint32_t symbol_count, const char *interval, uint32_t request_id,
    char *out, size_t out_sz)
{
  size_t used = 0;
  uint32_t i;
  uint32_t stream_count = 0;
  int n;

  if(method == NULL || symbols == NULL || symbol_count == 0 || interval == NULL
      || out == NULL || out_sz == 0)
    return(false);

  n = snprintf(out, out_sz, "{\"method\":\"%s\",\"params\":[", method);
  if(n < 0 || (size_t)n >= out_sz)
    return(false);
  used = (size_t)n;

  for(i = 0; i < symbol_count; i++)
  {
    char stream[BNB_STREAM_SZ];
    if(symbols[i] == NULL || symbols[i][0] == '\0')
      continue;
    if(!bnb_ws_build_stream_name(symbols[i], interval, stream, sizeof(stream)))
      return(false);
    n = snprintf(out + used, out_sz - used, "%s\"%s\"",
        stream_count == 0 ? "" : ",",
        stream);
    if(n < 0 || (size_t)n >= out_sz - used)
      return(false);
    used += (size_t)n;
    stream_count++;
  }

  n = snprintf(out + used, out_sz - used, "],\"id\":%u}", request_id);
  if(n < 0 || (size_t)n >= out_sz - used)
    return(false);
  return(strstr(out, "@kline_") != NULL);
}

// Build a public SUBSCRIBE payload for Binance combined streams.
// returns: true if output fit and at least one symbol was included
bool
bnb_ws_build_subscribe_payload(const char * const *symbols,
    uint32_t symbol_count, const char *interval, uint32_t request_id,
    char *out, size_t out_sz)
{
  return(bnb_ws_build_stream_method_payload("SUBSCRIBE", symbols, symbol_count,
        interval, request_id, out, out_sz));
}

// Build a public UNSUBSCRIBE payload for Binance combined streams.
// returns: true if output fit and at least one symbol was included
bool
bnb_ws_build_unsubscribe_payload(const char * const *symbols,
    uint32_t symbol_count, const char *interval, uint32_t request_id,
    char *out, size_t out_sz)
{
  return(bnb_ws_build_stream_method_payload("UNSUBSCRIBE", symbols, symbol_count,
        interval, request_id, out, out_sz));
}

// Build a public LIST_SUBSCRIPTIONS payload for Binance WebSocket control.
// returns: true if output fit
bool
bnb_ws_build_list_subscriptions_payload(uint32_t request_id, char *out,
    size_t out_sz)
{
  int n;

  if(out == NULL || out_sz == 0)
    return(false);

  n = snprintf(out, out_sz, "{\"method\":\"LIST_SUBSCRIPTIONS\",\"id\":%u}",
      request_id);
  return(n > 0 && (size_t)n < out_sz);
}
