// =======================================================================
// Binance public market-data subscription registry.
//
// This helper tracks symbols for public kline subscriptions only. It does
// not connect to Binance, place orders, read account state, or touch credentials.
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

#include <ctype.h>
#include <stdio.h>

static void
bnb_symbol_upper(char *dst, size_t dst_sz, const char *src)
{
  size_t i;

  if(dst == NULL || dst_sz == 0)
    return;
  dst[0] = '\0';
  if(src == NULL)
    return;

  while(*src != '\0' && isspace((unsigned char)*src))
    src++;

  for(i = 0; i + 1 < dst_sz && src[i] != '\0'; i++)
  {
    if(isspace((unsigned char)src[i]))
      break;
    dst[i] = (char)toupper((unsigned char)src[i]);
  }
  dst[i] = '\0';
}

static bool
bnb_symbol_valid(const char *symbol)
{
  size_t i;

  if(symbol == NULL || symbol[0] == '\0')
    return(false);

  for(i = 0; symbol[i] != '\0'; i++)
  {
    if(!isalnum((unsigned char)symbol[i]))
      return(false);
  }
  return(true);
}

static int
bnb_find_slot_locked(const bnb_subscription_table_t *table, const char *symbol)
{
  uint32_t i;

  for(i = 0; i < BNB_MAX_SYMBOLS; i++)
  {
    if(table->subs[i].symbol[0] == '\0')
      continue;
    if(strncasecmp(table->subs[i].symbol, symbol, sizeof(table->subs[i].symbol)) == 0)
      return((int)i);
  }
  return(-1);
}

void
bnb_subscription_table_init(bnb_subscription_table_t *table)
{
  if(table == NULL)
    return;
  memset(table, 0, sizeof(*table));
  pthread_rwlock_init(&table->rwl, NULL);
}

void
bnb_subscription_table_destroy(bnb_subscription_table_t *table)
{
  uint32_t i;

  if(table == NULL)
    return;

  pthread_rwlock_wrlock(&table->rwl);
  for(i = 0; i < BNB_MAX_SYMBOLS; i++)
  {
    if(table->subs[i].symbol[0] != '\0')
      pthread_mutex_destroy(&table->subs[i].mu);
  }
  table->count = 0;
  pthread_rwlock_unlock(&table->rwl);
  pthread_rwlock_destroy(&table->rwl);
}

bool
bnb_subscription_table_add(bnb_subscription_table_t *table, const char *symbol)
{
  char normalized[BNB_SYMBOL_SZ];
  uint32_t i;

  if(table == NULL)
    return(false);

  bnb_symbol_upper(normalized, sizeof(normalized), symbol);
  if(!bnb_symbol_valid(normalized))
    return(false);

  pthread_rwlock_wrlock(&table->rwl);
  if(bnb_find_slot_locked(table, normalized) >= 0)
  {
    pthread_rwlock_unlock(&table->rwl);
    return(true);
  }

  for(i = 0; i < BNB_MAX_SYMBOLS; i++)
  {
    if(table->subs[i].symbol[0] != '\0')
      continue;
    snprintf(table->subs[i].symbol, sizeof(table->subs[i].symbol), "%s", normalized);
    memset(&table->subs[i].current_bar, 0, sizeof(table->subs[i].current_bar));
    pthread_mutex_init(&table->subs[i].mu, NULL);
    table->count++;
    pthread_rwlock_unlock(&table->rwl);
    return(true);
  }

  pthread_rwlock_unlock(&table->rwl);
  return(false);
}

uint32_t
bnb_subscription_table_add_csv(bnb_subscription_table_t *table,
    const char *symbols_csv)
{
  char token[BNB_SYMBOL_SZ];
  uint32_t added = 0;
  size_t i = 0;
  size_t n = 0;

  if(table == NULL || symbols_csv == NULL)
    return(0);

  for(i = 0; ; i++)
  {
    char ch = symbols_csv[i];
    if(ch == ',' || ch == '\0')
    {
      uint32_t before_count;
      token[n] = '\0';
      before_count = bnb_subscription_table_count(table);
      if(token[0] != '\0' && bnb_subscription_table_add(table, token)
          && bnb_subscription_table_count(table) > before_count)
        added++;
      n = 0;
      if(ch == '\0')
        break;
      continue;
    }

    if(n + 1 < sizeof(token))
      token[n++] = ch;
  }

  return(added);
}

bool
bnb_subscription_table_remove(bnb_subscription_table_t *table, const char *symbol)
{
  char normalized[BNB_SYMBOL_SZ];
  int idx;

  if(table == NULL)
    return(false);

  bnb_symbol_upper(normalized, sizeof(normalized), symbol);
  if(!bnb_symbol_valid(normalized))
    return(false);

  pthread_rwlock_wrlock(&table->rwl);
  idx = bnb_find_slot_locked(table, normalized);
  if(idx < 0)
  {
    pthread_rwlock_unlock(&table->rwl);
    return(false);
  }

  pthread_mutex_destroy(&table->subs[idx].mu);
  memset(&table->subs[idx], 0, sizeof(table->subs[idx]));
  if(table->count > 0)
    table->count--;
  pthread_rwlock_unlock(&table->rwl);
  return(true);
}

bool
bnb_subscription_table_contains(const bnb_subscription_table_t *table, const char *symbol)
{
  char normalized[BNB_SYMBOL_SZ];
  bnb_subscription_table_t *mutable_table = (bnb_subscription_table_t *)table;
  bool found;

  if(table == NULL)
    return(false);

  bnb_symbol_upper(normalized, sizeof(normalized), symbol);
  if(!bnb_symbol_valid(normalized))
    return(false);

  pthread_rwlock_rdlock(&mutable_table->rwl);
  found = bnb_find_slot_locked(table, normalized) >= 0;
  pthread_rwlock_unlock(&mutable_table->rwl);
  return(found);
}

uint32_t
bnb_subscription_table_count(const bnb_subscription_table_t *table)
{
  uint32_t count;
  bnb_subscription_table_t *mutable_table = (bnb_subscription_table_t *)table;

  if(table == NULL)
    return(0);

  pthread_rwlock_rdlock(&mutable_table->rwl);
  count = table->count;
  pthread_rwlock_unlock(&mutable_table->rwl);
  return(count);
}

uint32_t
bnb_subscription_table_snapshot(const bnb_subscription_table_t *table,
    char symbols[][BNB_SYMBOL_SZ], uint32_t max_symbols)
{
  uint32_t i;
  uint32_t count = 0;
  bnb_subscription_table_t *mutable_table = (bnb_subscription_table_t *)table;

  if(table == NULL || symbols == NULL || max_symbols == 0)
    return(0);

  pthread_rwlock_rdlock(&mutable_table->rwl);
  for(i = 0; i < BNB_MAX_SYMBOLS && count < max_symbols; i++)
  {
    if(table->subs[i].symbol[0] == '\0')
      continue;
    snprintf(symbols[count], BNB_SYMBOL_SZ, "%s", table->subs[i].symbol);
    count++;
  }
  pthread_rwlock_unlock(&mutable_table->rwl);
  return(count);
}

bool
bnb_subscription_table_update_bar(bnb_subscription_table_t *table,
    const bnb_bar_t *bar)
{
  int idx;

  if(table == NULL || bar == NULL || bar->symbol[0] == '\0')
    return(false);

  pthread_rwlock_rdlock(&table->rwl);
  idx = bnb_find_slot_locked(table, bar->symbol);
  if(idx < 0)
  {
    pthread_rwlock_unlock(&table->rwl);
    return(false);
  }

  pthread_mutex_lock(&table->subs[idx].mu);
  table->subs[idx].current_bar = *bar;
  pthread_mutex_unlock(&table->subs[idx].mu);
  pthread_rwlock_unlock(&table->rwl);
  return(true);
}

bool
bnb_subscription_table_get_bar(const bnb_subscription_table_t *table,
    const char *symbol, bnb_bar_t *out)
{
  char normalized[BNB_SYMBOL_SZ];
  bnb_subscription_table_t *mutable_table = (bnb_subscription_table_t *)table;
  int idx;

  if(table == NULL || out == NULL)
    return(false);

  bnb_symbol_upper(normalized, sizeof(normalized), symbol);
  if(!bnb_symbol_valid(normalized))
    return(false);

  pthread_rwlock_rdlock(&mutable_table->rwl);
  idx = bnb_find_slot_locked(table, normalized);
  if(idx < 0)
  {
    pthread_rwlock_unlock(&mutable_table->rwl);
    return(false);
  }

  pthread_mutex_lock(&mutable_table->subs[idx].mu);
  *out = mutable_table->subs[idx].current_bar;
  pthread_mutex_unlock(&mutable_table->subs[idx].mu);
  pthread_rwlock_unlock(&mutable_table->rwl);
  return(out->symbol[0] != '\0');
}

bool
bnb_subscription_table_build_subscribe_payload(
    const bnb_subscription_table_t *table, const char *interval,
    uint32_t request_id, char *out, size_t out_sz)
{
  char symbols[BNB_MAX_SYMBOLS][BNB_SYMBOL_SZ];
  const char *symbol_ptrs[BNB_MAX_SYMBOLS];
  uint32_t i;
  uint32_t count;

  if(table == NULL || out == NULL || out_sz == 0)
    return(false);

  count = bnb_subscription_table_snapshot(table, symbols, BNB_MAX_SYMBOLS);
  for(i = 0; i < count; i++)
    symbol_ptrs[i] = symbols[i];

  if(count == 0)
    return(false);
  return(bnb_ws_build_subscribe_payload(symbol_ptrs, count, interval, request_id, out, out_sz));
}

bool
bnb_subscription_table_build_unsubscribe_payload(
    const bnb_subscription_table_t *table, const char *interval,
    uint32_t request_id, char *out, size_t out_sz)
{
  char symbols[BNB_MAX_SYMBOLS][BNB_SYMBOL_SZ];
  const char *symbol_ptrs[BNB_MAX_SYMBOLS];
  uint32_t i;
  uint32_t count;

  if(table == NULL || out == NULL || out_sz == 0)
    return(false);

  count = bnb_subscription_table_snapshot(table, symbols, BNB_MAX_SYMBOLS);
  for(i = 0; i < count; i++)
    symbol_ptrs[i] = symbols[i];

  if(count == 0)
    return(false);
  return(bnb_ws_build_unsubscribe_payload(symbol_ptrs, count, interval, request_id,
        out, out_sz));
}

bool
bnb_subscription_table_build_combined_stream_url(
    const bnb_subscription_table_t *table, const char *base_url,
    const char *interval, char *out, size_t out_sz)
{
  char symbols[BNB_MAX_SYMBOLS][BNB_SYMBOL_SZ];
  const char *symbol_ptrs[BNB_MAX_SYMBOLS];
  uint32_t i;
  uint32_t count;

  if(table == NULL || out == NULL || out_sz == 0)
    return(false);

  count = bnb_subscription_table_snapshot(table, symbols, BNB_MAX_SYMBOLS);
  for(i = 0; i < count; i++)
    symbol_ptrs[i] = symbols[i];

  if(count == 0)
    return(false);
  return(bnb_ws_build_combined_stream_url(base_url, symbol_ptrs, count, interval,
        out, out_sz));
}
