// =======================================================================
// Binance public market-data frame dispatch.
//
// Parses public kline frames, verifies they belong to an active public
// subscription, updates the latest per-subscription bar, and caches finalized
// bars. This file does not open sockets, call REST, submit orders, or read
// account state.
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

bool
bnb_market_data_apply_kline_frame(bnb_subscription_table_t *table,
    bnb_bar_cache_t *cache, const char *frame, const char *expected_interval,
    bnb_bar_t *out)
{
  bnb_bar_t bar;
  char interval[BNB_INTERVAL_SZ];

  if(table == NULL || cache == NULL || frame == NULL)
    return(false);

  if(!bnb_ws_parse_kline_frame(frame, &bar, interval, sizeof(interval)))
    return(false);

  if(expected_interval != NULL && expected_interval[0] != '\0'
      && strcmp(interval, expected_interval) != 0)
    return(false);

  if(!bnb_subscription_table_update_bar(table, &bar))
    return(false);

  if(bar.finalized && !bnb_bar_cache_upsert(cache, &bar))
    return(false);

  if(out != NULL)
    *out = bar;
  return(true);
}
