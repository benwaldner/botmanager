// =======================================================================
// Binance finalized-bar cache.
//
// Market-data-only cache for public OHLCV bars. No account state, order
// state, private endpoint state, or trading decisions live here.
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

void
bnb_bar_cache_init(bnb_bar_cache_t *cache)
{
  if(cache == NULL)
    return;
  memset(cache, 0, sizeof(*cache));
  pthread_mutex_init(&cache->mu, NULL);
}

void
bnb_bar_cache_destroy(bnb_bar_cache_t *cache)
{
  if(cache == NULL)
    return;
  pthread_mutex_destroy(&cache->mu);
}

bool
bnb_bar_cache_upsert(bnb_bar_cache_t *cache, const bnb_bar_t *bar)
{
  uint32_t i;

  if(cache == NULL || bar == NULL || bar->symbol[0] == '\0')
    return(false);

  pthread_mutex_lock(&cache->mu);
  for(i = 0; i < cache->count; i++)
  {
    if(strncmp(cache->bars[i].symbol, bar->symbol, sizeof(cache->bars[i].symbol)) == 0)
    {
      cache->bars[i] = *bar;
      pthread_mutex_unlock(&cache->mu);
      return(true);
    }
  }

  if(cache->count >= BNB_MAX_SYMBOLS)
  {
    pthread_mutex_unlock(&cache->mu);
    return(false);
  }

  cache->bars[cache->count++] = *bar;
  pthread_mutex_unlock(&cache->mu);
  return(true);
}

bool
bnb_bar_cache_get(const bnb_bar_cache_t *cache, const char *symbol,
    bnb_bar_t *out)
{
  uint32_t i;
  bnb_bar_cache_t *mutable_cache = (bnb_bar_cache_t *)cache;

  if(cache == NULL || symbol == NULL || out == NULL)
    return(false);

  pthread_mutex_lock(&mutable_cache->mu);
  for(i = 0; i < cache->count; i++)
  {
    if(strncasecmp(cache->bars[i].symbol, symbol, sizeof(cache->bars[i].symbol)) == 0)
    {
      *out = cache->bars[i];
      pthread_mutex_unlock(&mutable_cache->mu);
      return(true);
    }
  }
  pthread_mutex_unlock(&mutable_cache->mu);
  return(false);
}

uint32_t
bnb_bar_cache_count(const bnb_bar_cache_t *cache)
{
  uint32_t count;
  bnb_bar_cache_t *mutable_cache = (bnb_bar_cache_t *)cache;

  if(cache == NULL)
    return(0);
  pthread_mutex_lock(&mutable_cache->mu);
  count = cache->count;
  pthread_mutex_unlock(&mutable_cache->mu);
  return(count);
}
