// =======================================================================
// score_pump_v3 bot plugin — v3 pump-score trading strategy.
//
// Provides MOM-24 + RVOL scoring over an internal per-symbol rolling
// history. The active periodic/live-order loop remains dormant until
// BotManager exposes cancellable per-instance tasks and a cross-plugin
// bar-cache interface.
//
// This plugin requires "service_binance" — data is fed via admin commands
// or future service plugin integration. The periodic task is a scaffold:
// production use should wire bnb_bar_cache_get() once the bar cache is
// exposed through a cross-plugin interface.
//
// KV configuration keys:
//   bot.<name>.dry_run        uint8   1     — 1 = log only, no orders
//   bot.<name>.history_len    uint32  24    — bars for MOM/RVOL window
//   bot.<name>.rvol_weight    str     0.5   — RVOL exponent
//   bot.<name>.min_score      str     0.005 — entry gate: min combined score
//   bot.<name>.min_rvol       str     1.5   — entry gate: min RVOL
//   bot.<name>.min_mom        str     0.003 — entry gate: min momentum
//   bot.<name>.top_n          uint32  4     — max simultaneous positions
//   bot.<name>.tick_ms        uint32  300000 — scoring interval (ms)
//
// Requires: service_binance (for market-data dependency declaration).
// Provides: bot_score_pump_v3.
// =======================================================================

#define SPV3_INTERNAL
#include "score_pump_v3.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -----------------------------------------------------------------------
// Symbol rolling-history helpers (exposed for tests via header)
// -----------------------------------------------------------------------

// Push a new bar into the symbol's circular history.
// Returns false on NULL input.
bool
spv3_sym_push(spv3_sym_state_t *s, double close, double volume)
{
  if(!s || close <= 0.0)
    return(false);

  s->closes [s->head] = close;
  s->volumes[s->head] = volume < 0.0 ? 0.0 : volume;
  s->head = (s->head + 1) % SPV3_HISTORY_LEN;
  if(s->count < SPV3_HISTORY_LEN)
    s->count++;

  return(true);
}

// Return true if the symbol has at least history_len bars.
bool
spv3_sym_ready(const spv3_sym_state_t *s, uint32_t history_len)
{
  return(s && history_len >= 2 && s->count >= history_len);
}

// Extract the oldest-first arrays (closes/volumes) of length history_len.
// out_closes and out_volumes must be at least history_len elements.
void
spv3_sym_get_arrays(const spv3_sym_state_t *s, uint32_t history_len,
                    double *out_closes, double *out_volumes)
{
  if(!s || !out_closes || !out_volumes || history_len == 0)
    return;

  uint32_t start = (s->head + SPV3_HISTORY_LEN - history_len) % SPV3_HISTORY_LEN;
  for(uint32_t i = 0; i < history_len; i++)
  {
    uint32_t idx    = (start + i) % SPV3_HISTORY_LEN;
    out_closes [i]  = s->closes [idx];
    out_volumes[i]  = s->volumes[idx];
  }
}

// -----------------------------------------------------------------------
// KV helpers
// -----------------------------------------------------------------------

static double
spv3_kv_double(const char *key, double fallback)
{
  const char *s = kv_get_str(key);
  if(!s || !*s)
    return(fallback);
  char *end;
  double v = strtod(s, &end);
  return(end != s ? v : fallback);
}

// -----------------------------------------------------------------------
// Tick task — scoring loop
// -----------------------------------------------------------------------

static void
spv3_tick(task_t *t)
{
  if(t == NULL)
    return;

  t->state = TASK_ENDED;

  spv3_state_t *st = (spv3_state_t *)t->data;
  if(!st)
    return;

  st->ticks++;

  // Refresh parameters from KV on every tick so live changes take effect.
  st->history_len                 = (uint32_t)kv_get_uint("bot.score_pump_v3.history_len");
  if(st->history_len < 2 || st->history_len > SPV3_HISTORY_LEN)
    st->history_len = SPV3_HISTORY_LEN;

  st->score_params.rvol_weight    = spv3_kv_double("bot.score_pump_v3.rvol_weight", 0.5);
  st->score_params.gate.min_score = spv3_kv_double("bot.score_pump_v3.min_score", 0.005);
  st->score_params.gate.min_rvol  = spv3_kv_double("bot.score_pump_v3.min_rvol",  1.5);
  st->score_params.gate.min_mom   = spv3_kv_double("bot.score_pump_v3.min_mom",   0.003);

  uint32_t top_n = (uint32_t)kv_get_uint("bot.score_pump_v3.top_n");
  if(top_n == 0 || top_n > SPV3_MAX_SYMBOLS)
    top_n = 4;

  uint32_t n = st->n_syms;
  if(n == 0)
  {
    clam(CLAM_DEBUG, SPV3_CTX, "tick %llu: no symbols loaded",
         (unsigned long long)st->ticks);
    return;
  }

  // Score all symbols.
  double   scores   [SPV3_MAX_SYMBOLS];
  bool     gate_pass[SPV3_MAX_SYMBOLS];
  double   sym_c    [SPV3_HISTORY_LEN];
  double   sym_v    [SPV3_HISTORY_LEN];

  for(uint32_t s = 0; s < n; s++)
  {
    if(!spv3_sym_ready(&st->syms[s], st->history_len))
    {
      scores[s] = SPV3_NAN;
      gate_pass[s] = false;
      continue;
    }
    spv3_sym_get_arrays(&st->syms[s], st->history_len, sym_c, sym_v);
    double mom  = spv3_mom_n(sym_c, st->history_len);
    double rvol = spv3_rvol(sym_v, st->history_len);
    scores[s]   = spv3_score(mom, rvol, st->score_params.rvol_weight);
    gate_pass[s]= spv3_gate_open(scores[s], rvol, mom, &st->score_params.gate);
  }

  // Find top-N passing symbols.
  uint32_t top_idx[SPV3_MAX_SYMBOLS];
  uint32_t top_cnt = spv3_top_n(scores, n, top_n, top_idx);

  uint8_t dry = (uint8_t)kv_get_uint("bot.score_pump_v3.dry_run");

  clam(CLAM_INFO, SPV3_CTX,
       "tick %llu: n_syms=%u top=%u dry=%u",
       (unsigned long long)st->ticks, n, top_cnt, (unsigned)dry);

  for(uint32_t i = 0; i < top_cnt; i++)
  {
    uint32_t idx = top_idx[i];
    if(!gate_pass[idx])
      continue;

    const char *sym = st->syms[idx].symbol;
    clam(CLAM_INFO, SPV3_CTX,
         "  [%u] %s score=%.4f %s",
         i, sym, scores[idx], dry ? "(DRY)" : "(LIVE)");

    if(dry)
    {
      st->entries_dry++;
      // dry_run: log intent, skip order placement.
    }
    // Live execution is intentionally not wired in this plugin yet.
    // Future order delegation must go through the guarded Binance service API.
  }
}

// -----------------------------------------------------------------------
// Bot driver lifecycle
// -----------------------------------------------------------------------

static void *
spv3_create(bot_inst_t *inst)
{
  spv3_state_t *st = calloc(1, sizeof(*st));
  if(!st)
    return(NULL);
  st->inst = inst;
  clam(CLAM_INFO, SPV3_CTX, "created");
  return(st);
}

static void
spv3_destroy(void *handle)
{
  free(handle);
}

static bool
spv3_start(void *handle)
{
  spv3_state_t *st = (spv3_state_t *)handle;
  if(!st)
    return(FAIL);

  uint32_t tick_ms = (uint32_t)kv_get_uint("bot.score_pump_v3.tick_ms");
  if(tick_ms < 1000)
    tick_ms = 300000;  // default 5 minutes

  (void)spv3_tick;

  st->tick_task = NULL;
  clam(CLAM_INFO, SPV3_CTX,
       "started dormant (tick_ms=%u, periodic task disabled until cancellable tasks/bar-cache API)",
       tick_ms);
  return(SUCCESS);
}

static void
spv3_stop(void *handle)
{
  spv3_state_t *st = (spv3_state_t *)handle;
  if(!st)
    return;
  st->tick_task = NULL;
  clam(CLAM_INFO, SPV3_CTX, "stopped (ticks=%llu entries_dry=%llu)",
       (unsigned long long)st->ticks,
       (unsigned long long)st->entries_dry);
}

static void
spv3_on_message(void *handle, const method_msg_t *msg)
{
  (void)handle;
  (void)msg;
  // Admin command dispatch handled by the command bot plugin.
  // This plugin does not process user messages directly.
}

// -----------------------------------------------------------------------
// Plugin descriptor
// -----------------------------------------------------------------------

static const bot_driver_t spv3_driver = {
  .name       = "score_pump_v3",
  .create     = spv3_create,
  .destroy    = spv3_destroy,
  .start      = spv3_start,
  .stop       = spv3_stop,
  .on_message = spv3_on_message,
};

static const plugin_kv_entry_t spv3_kv_schema[] = {
  { "bot.score_pump_v3.dry_run",     KV_UINT8,  "1"      },
  { "bot.score_pump_v3.history_len", KV_UINT32, "24"     },
  { "bot.score_pump_v3.rvol_weight", KV_STR,    "0.5"    },
  { "bot.score_pump_v3.min_score",   KV_STR,    "0.005"  },
  { "bot.score_pump_v3.min_rvol",    KV_STR,    "1.5"    },
  { "bot.score_pump_v3.min_mom",     KV_STR,    "0.003"  },
  { "bot.score_pump_v3.top_n",       KV_UINT32, "4"      },
  { "bot.score_pump_v3.tick_ms",     KV_UINT32, "300000" },
};

const plugin_desc_t bm_plugin_desc = {
  .api_version     = PLUGIN_API_VERSION,
  .name            = "score_pump_v3",
  .version         = "0.1",
  .type            = PLUGIN_BOT,
  .kind            = "score_pump_v3",

  .provides        = { { .name = "bot_score_pump_v3" } },
  .provides_count  = 1,
  .requires        = { { .name = "service_binance" } },
  .requires_count  = 1,

  .kv_schema       = spv3_kv_schema,
  .kv_schema_count = sizeof(spv3_kv_schema) / sizeof(spv3_kv_schema[0]),

  .init            = NULL,
  .start           = NULL,
  .stop            = NULL,
  .deinit          = NULL,

  .ext             = (void *)&spv3_driver,
};
