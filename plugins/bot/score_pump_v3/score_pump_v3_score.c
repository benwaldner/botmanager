// =======================================================================
// score_pump_v3 — pure scoring functions (no core dependencies).
//
// Implements the v3 pump-detection scoring algorithm in C:
//   MOM-N   : N-bar momentum  = (close_last - close_first) / close_first
//   RVOL    : relative volume = last_volume / mean(volume[0..N-2])
//   score   : mom * rvol^rvol_weight  (signed; negative mom → negative score)
//   gate    : score >= min_score AND rvol >= min_rvol
//
// All functions in this file operate on caller-supplied arrays and produce
// deterministic output. No heap allocation, no global state, no botmanager
// core symbols — compilable and testable as a standalone translation unit.
// =======================================================================

#define SPV3_INTERNAL
#include "score_pump_v3.h"

#include <math.h>
#include <string.h>

// -----------------------------------------------------------------------
// MOM-N
// -----------------------------------------------------------------------

// Compute N-bar momentum from a close price array.
// closes[0] is the oldest bar, closes[n-1] is the newest.
// Returns SPV3_NAN on invalid input (n < 2, zero/negative prices).
double
spv3_mom_n(const double *closes, uint32_t n)
{
  if(!closes || n < 2)
    return(SPV3_NAN);

  double first = closes[0];
  double last  = closes[n - 1];

  if(first <= 0.0 || last <= 0.0)
    return(SPV3_NAN);

  return((last - first) / first);
}

// -----------------------------------------------------------------------
// RVOL — relative volume
// -----------------------------------------------------------------------

// Compute relative volume: last bar's volume divided by the mean of the
// preceding (n-1) bars' volumes.
// volumes[0..n-2] are the reference bars; volumes[n-1] is the current bar.
// Returns 1.0 (neutral) when the reference mean is zero or n < 2.
double
spv3_rvol(const double *volumes, uint32_t n)
{
  if(!volumes || n < 2)
    return(1.0);

  double sum = 0.0;
  for(uint32_t i = 0; i < n - 1; i++)
    sum += volumes[i];

  double mean_vol = sum / (double)(n - 1);
  if(mean_vol <= 0.0)
    return(1.0);

  double last_vol = volumes[n - 1];
  if(last_vol < 0.0)
    return(1.0);

  return(last_vol / mean_vol);
}

// -----------------------------------------------------------------------
// Combined score
// -----------------------------------------------------------------------

// Compute the combined pump score for a symbol.
// score = mom * pow(max(rvol, 0), rvol_weight)
// Preserves the sign of mom. SPV3_NAN mom propagates as SPV3_NAN.
// rvol_weight=0.5 matches the Python v3 default.
double
spv3_score(double mom, double rvol, double rvol_weight)
{
  if(isnan(mom))
    return(SPV3_NAN);

  double rv = (rvol < 0.0) ? 0.0 : rvol;
  double rv_factor = (rvol_weight == 0.0) ? 1.0 : pow(rv, rvol_weight);
  return(mom * rv_factor);
}

// -----------------------------------------------------------------------
// Entry gate
// -----------------------------------------------------------------------

// Return true if a symbol passes the entry gate.
// Gate: score >= min_score AND rvol >= min_rvol AND mom >= min_mom.
bool
spv3_gate_open(double score, double rvol, double mom,
               const spv3_gate_params_t *gate)
{
  if(!gate)
    return(false);
  if(isnan(score) || isnan(rvol) || isnan(mom))
    return(false);
  return(score >= gate->min_score
         && rvol  >= gate->min_rvol
         && mom   >= gate->min_mom);
}

// -----------------------------------------------------------------------
// Batch scoring
// -----------------------------------------------------------------------

// Score an array of symbols from a flat row-major bar matrix.
// bars[sym][bar] = closes[sym * history_len + bar] for close prices.
// Results are written to out[0..n_symbols-1]; SPV3_NAN for invalid input.
// Returns the number of symbols that passed the gate.
uint32_t
spv3_score_batch(const double          *closes,
                 const double          *volumes,
                 uint32_t               n_symbols,
                 uint32_t               history_len,
                 const spv3_score_params_t *params,
                 double                *out_scores,
                 bool                  *out_gate)
{
  if(!closes || !volumes || !params || !out_scores || n_symbols == 0
     || history_len < 2)
    return(0);

  uint32_t passed = 0;

  for(uint32_t s = 0; s < n_symbols; s++)
  {
    const double *sc = closes  + s * history_len;
    const double *sv = volumes + s * history_len;

    double mom   = spv3_mom_n(sc, history_len);
    double rvol  = spv3_rvol(sv, history_len);
    double score = spv3_score(mom, rvol, params->rvol_weight);

    out_scores[s] = score;

    if(out_gate)
    {
      bool gate = spv3_gate_open(score, rvol, mom, &params->gate);
      out_gate[s] = gate;
      if(gate)
        passed++;
    }
  }

  return(passed);
}

// -----------------------------------------------------------------------
// Ranked top-N selection (simple insertion sort, n_symbols usually < 200)
// -----------------------------------------------------------------------

// Fill out_indices[0..top_n-1] with the indices of the top_n highest-scoring
// symbols (excluding NaN). Returns the number of valid entries written.
uint32_t
spv3_top_n(const double *scores, uint32_t n_symbols,
           uint32_t top_n, uint32_t *out_indices)
{
  if(!scores || !out_indices || n_symbols == 0 || top_n == 0)
    return(0);

  uint32_t count = 0;

  for(uint32_t s = 0; s < n_symbols; s++)
  {
    if(isnan(scores[s]))
      continue;

    if(count < top_n)
    {
      out_indices[count++] = s;
      // Bubble the new entry up to its sorted position.
      for(uint32_t k = count - 1; k > 0; k--)
      {
        if(scores[out_indices[k]] > scores[out_indices[k - 1]])
        {
          uint32_t tmp          = out_indices[k];
          out_indices[k]        = out_indices[k - 1];
          out_indices[k - 1]    = tmp;
        }
        else
          break;
      }
    }
    else if(scores[s] > scores[out_indices[top_n - 1]])
    {
      out_indices[top_n - 1] = s;
      for(uint32_t k = top_n - 1; k > 0; k--)
      {
        if(scores[out_indices[k]] > scores[out_indices[k - 1]])
        {
          uint32_t tmp          = out_indices[k];
          out_indices[k]        = out_indices[k - 1];
          out_indices[k - 1]    = tmp;
        }
        else
          break;
      }
    }
  }

  return(count);
}
