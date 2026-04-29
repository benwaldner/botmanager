// =======================================================================
// Deterministic Binance public WebSocket reconnect backoff helper.
//
// This file only computes capped backoff durations. It does not sleep,
// open sockets, call Binance, or touch private/account/order paths.
// =======================================================================

#define BNB_INTERNAL
#include "binance.h"

bool
bnb_ws_reconnect_backoff_ms(uint32_t base_ms, uint32_t attempt,
    uint32_t cap_ms, uint32_t *out_ms)
{
  uint64_t delay;

  if(base_ms == 0 || cap_ms == 0 || out_ms == NULL)
    return(false);

  delay = base_ms;
  while(attempt > 0 && delay < cap_ms)
  {
    delay *= 2;
    if(delay > cap_ms)
      delay = cap_ms;
    attempt--;
  }

  if(delay > cap_ms)
    delay = cap_ms;

  *out_ms = (uint32_t)delay;
  return(true);
}
