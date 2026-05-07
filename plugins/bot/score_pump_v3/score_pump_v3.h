#ifndef BM_SCORE_PUMP_V3_H
#define BM_SCORE_PUMP_V3_H

// =======================================================================
// score_pump_v3 bot plugin header.
//
// No exported C API — this plugin interacts with the core through bot/task
// interfaces. All declarations are internal (BNB_INTERNAL-style guard).
//
// Scoring constants visible outside BNB_INTERNAL so tests can include
// this header without dragging in all of the core includes.
// =======================================================================

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

// Sentinel for invalid / uncomputed scores.
#define SPV3_NAN  (double)NAN

// -----------------------------------------------------------------------
// Scoring parameters (pure — no core dependencies)
// -----------------------------------------------------------------------

// Entry gate thresholds.
typedef struct
{
  double min_score;    // minimum combined score to enter (default 0.005)
  double min_rvol;     // minimum relative volume       (default 1.5)
  double min_mom;      // minimum N-bar momentum        (default 0.003)
} spv3_gate_params_t;

// Full scoring configuration for a batch evaluation.
typedef struct
{
  double           rvol_weight;  // RVOL exponent in score = mom * rvol^w (default 0.5)
  spv3_gate_params_t gate;
} spv3_score_params_t;

// -----------------------------------------------------------------------
// Pure scoring API (score_pump_v3_score.c)
// -----------------------------------------------------------------------

// N-bar momentum: (closes[n-1] - closes[0]) / closes[0].
// Returns SPV3_NAN on invalid input.
double spv3_mom_n(const double *closes, uint32_t n);

// Relative volume: volumes[n-1] / mean(volumes[0..n-2]).
// Returns 1.0 when insufficient data.
double spv3_rvol(const double *volumes, uint32_t n);

// Combined score: mom * rvol^rvol_weight.  Sign preserved from mom.
double spv3_score(double mom, double rvol, double rvol_weight);

// Entry gate: score >= min_score AND rvol >= min_rvol AND mom >= min_mom.
bool spv3_gate_open(double score, double rvol, double mom,
                    const spv3_gate_params_t *gate);

// Score n_symbols symbols from flat row-major close/volume matrices.
// Returns number of symbols passing the gate.
uint32_t spv3_score_batch(const double *closes, const double *volumes,
                          uint32_t n_symbols, uint32_t history_len,
                          const spv3_score_params_t *params,
                          double *out_scores, bool *out_gate);

// Return indices of the top_n highest-scoring symbols (descending).
uint32_t spv3_top_n(const double *scores, uint32_t n_symbols,
                    uint32_t top_n, uint32_t *out_indices);


#ifdef SPV3_INTERNAL

#include "bot.h"
#include "clam.h"
#include "common.h"
#include "kv.h"
#include "plugin.h"
#include "task.h"

// Plugin context string for clam logging.
#define SPV3_CTX           "score_pump_v3"

// Maximum symbols the bot can track simultaneously.
#define SPV3_MAX_SYMBOLS   128

// Rolling bar history length per symbol (24 × 5m = 2h look-back).
#define SPV3_HISTORY_LEN   24

// -----------------------------------------------------------------------
// Per-symbol rolling bar state
// -----------------------------------------------------------------------

typedef struct
{
  char     symbol[16];
  double   closes [SPV3_HISTORY_LEN];
  double   volumes[SPV3_HISTORY_LEN];
  uint32_t count;     // number of bars loaded (0..SPV3_HISTORY_LEN)
  uint32_t head;      // circular-buffer write position
} spv3_sym_state_t;

// -----------------------------------------------------------------------
// Per-bot-instance state
// -----------------------------------------------------------------------

typedef struct
{
  bot_inst_t      *inst;
  task_t          *tick_task;

  spv3_sym_state_t syms[SPV3_MAX_SYMBOLS];
  uint32_t         n_syms;

  // Cached KV-driven parameters (refreshed each tick).
  uint32_t         history_len;
  spv3_score_params_t score_params;

  // Statistics.
  uint64_t         ticks;
  uint64_t         entries_dry;
} spv3_state_t;

// -----------------------------------------------------------------------
// Internal helpers (score_pump_v3.c)
// -----------------------------------------------------------------------

static void *spv3_create(bot_inst_t *inst);
static void  spv3_destroy(void *handle);
static bool  spv3_start(void *handle);
static void  spv3_stop(void *handle);
static void  spv3_on_message(void *handle, const method_msg_t *msg);
static void  spv3_tick(task_t *t);

// Symbol state helpers.
bool spv3_sym_push(spv3_sym_state_t *s, double close, double volume);
bool spv3_sym_ready(const spv3_sym_state_t *s, uint32_t history_len);
void spv3_sym_get_arrays(const spv3_sym_state_t *s, uint32_t history_len,
                         double *out_closes, double *out_volumes);

#endif // SPV3_INTERNAL

#endif // BM_SCORE_PUMP_V3_H
