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
  return(json_object_get_string(value));
}

static int64_t
bnb_json_get_i64(struct json_object *obj, const char *key)
{
  struct json_object *value = NULL;
  if(!bnb_json_get_obj(obj, key, &value))
    return(0);
  return((int64_t)json_object_get_int64(value));
}

static uint32_t
bnb_json_get_u32(struct json_object *obj, const char *key)
{
  struct json_object *value = NULL;
  if(!bnb_json_get_obj(obj, key, &value))
    return(0);
  return((uint32_t)json_object_get_int64(value));
}

static double
bnb_json_get_double_str(struct json_object *obj, const char *key)
{
  const char *text = bnb_json_get_str(obj, key);
  if(text == NULL)
    return(0.0);
  return(strtod(text, NULL));
}

static bool
bnb_json_get_bool(struct json_object *obj, const char *key)
{
  struct json_object *value = NULL;
  if(!bnb_json_get_obj(obj, key, &value))
    return(false);
  return(json_object_get_boolean(value));
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

// Parse one Binance combined-stream kline frame into a finalized/partial bar.
// returns: true on successful parse, false on invalid or unsupported frame
bool
bnb_ws_parse_kline_frame(const char *frame, bnb_bar_t *out,
    char *interval, size_t interval_sz)
{
  struct json_object *root = NULL;
  struct json_object *data = NULL;
  struct json_object *kline = NULL;
  const char *event_type = NULL;
  const char *symbol = NULL;
  const char *kline_symbol = NULL;
  const char *kline_interval = NULL;

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

  symbol = bnb_json_get_str(data, "s");
  kline_symbol = bnb_json_get_str(kline, "s");
  kline_interval = bnb_json_get_str(kline, "i");
  if(kline_symbol != NULL)
    symbol = kline_symbol;
  if(symbol == NULL || kline_interval == NULL)
  {
    json_object_put(root);
    return(false);
  }

  memset(out, 0, sizeof(*out));
  bnb_copy_upper(out->symbol, sizeof(out->symbol), symbol);
  out->open_time_ms = bnb_json_get_i64(kline, "t");
  out->close_time_ms = bnb_json_get_i64(kline, "T");
  out->open = bnb_json_get_double_str(kline, "o");
  out->high = bnb_json_get_double_str(kline, "h");
  out->low = bnb_json_get_double_str(kline, "l");
  out->close = bnb_json_get_double_str(kline, "c");
  out->volume_base = bnb_json_get_double_str(kline, "v");
  out->volume_quote = bnb_json_get_double_str(kline, "q");
  out->trade_count = bnb_json_get_u32(kline, "n");
  out->finalized = bnb_json_get_bool(kline, "x");

  if(interval != NULL && interval_sz > 0)
    snprintf(interval, interval_sz, "%s", kline_interval);

  json_object_put(root);
  return(out->symbol[0] != '\0' && out->open_time_ms > 0 && out->close_time_ms > 0);
}

// Build one stream name such as "solusdt@kline_5m".
// returns: true if output fit
bool
bnb_ws_build_stream_name(const char *symbol, const char *interval,
    char *out, size_t out_sz)
{
  char lower_symbol[BNB_SYMBOL_SZ];
  int n;

  if(symbol == NULL || interval == NULL || out == NULL || out_sz == 0)
    return(false);

  bnb_copy_lower(lower_symbol, sizeof(lower_symbol), symbol);
  n = snprintf(out, out_sz, "%s@kline_%s", lower_symbol, interval);
  return(n > 0 && (size_t)n < out_sz);
}

// Convert configured bar size to a Binance public kline interval.
// returns: true for supported intervals, false for unsupported values
bool
bnb_interval_from_bar_seconds(uint32_t bar_seconds, char *out, size_t out_sz)
{
  const char *interval = NULL;
  int n;

  if(out == NULL || out_sz == 0)
    return(false);

  switch(bar_seconds)
  {
    case 60:     interval = "1m"; break;
    case 180:    interval = "3m"; break;
    case 300:    interval = "5m"; break;
    case 900:    interval = "15m"; break;
    case 1800:   interval = "30m"; break;
    case 3600:   interval = "1h"; break;
    case 7200:   interval = "2h"; break;
    case 14400:  interval = "4h"; break;
    case 21600:  interval = "6h"; break;
    case 28800:  interval = "8h"; break;
    case 43200:  interval = "12h"; break;
    case 86400:  interval = "1d"; break;
    case 259200: interval = "3d"; break;
    case 604800: interval = "1w"; break;
    default:
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
