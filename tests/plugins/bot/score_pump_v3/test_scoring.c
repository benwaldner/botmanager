// Tests for score_pump_v3_score.c — pure scoring functions.
// No core dependencies. Offline, deterministic.

#include "../../../../plugins/bot/score_pump_v3/score_pump_v3.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NEAR(a, b, eps) (fabs((a) - (b)) < (eps))

// -----------------------------------------------------------------------
// spv3_mom_n
// -----------------------------------------------------------------------

static void
test_mom_n_basic_positive(void)
{
  double c[] = {1.0, 1.05, 1.10};
  double m   = spv3_mom_n(c, 3);
  assert(NEAR(m, 0.10, 1e-9));
}

static void
test_mom_n_basic_negative(void)
{
  double c[] = {1.0, 0.90};
  double m   = spv3_mom_n(c, 2);
  assert(NEAR(m, -0.10, 1e-9));
}

static void
test_mom_n_flat(void)
{
  double c[] = {1.0, 1.0, 1.0};
  assert(NEAR(spv3_mom_n(c, 3), 0.0, 1e-12));
}

static void
test_mom_n_rejects_null(void)
{
  assert(isnan(spv3_mom_n(NULL, 3)));
}

static void
test_mom_n_rejects_n_less_than_2(void)
{
  double c[] = {1.0};
  assert(isnan(spv3_mom_n(c, 1)));
  assert(isnan(spv3_mom_n(c, 0)));
}

static void
test_mom_n_rejects_zero_price(void)
{
  double c[] = {0.0, 1.0};
  assert(isnan(spv3_mom_n(c, 2)));
  double c2[] = {1.0, 0.0};
  assert(isnan(spv3_mom_n(c2, 2)));
}

static void
test_mom_n_uses_first_and_last_only(void)
{
  // Middle bars should be ignored.
  double c[] = {2.0, 999.0, 999.0, 3.0};
  assert(NEAR(spv3_mom_n(c, 4), 0.5, 1e-9));  // (3-2)/2 = 0.5
}

// -----------------------------------------------------------------------
// spv3_rvol
// -----------------------------------------------------------------------

static void
test_rvol_basic(void)
{
  // Reference bars: 100,100,100 (mean=100). Last bar: 200 → rvol=2.0
  double v[] = {100.0, 100.0, 100.0, 200.0};
  assert(NEAR(spv3_rvol(v, 4), 2.0, 1e-9));
}

static void
test_rvol_below_one(void)
{
  double v[] = {200.0, 200.0, 50.0};
  assert(NEAR(spv3_rvol(v, 3), 0.25, 1e-9));  // 50 / 200 = 0.25
}

static void
test_rvol_neutral_when_null(void)
{
  assert(NEAR(spv3_rvol(NULL, 3), 1.0, 1e-12));
}

static void
test_rvol_neutral_when_n_less_than_2(void)
{
  double v[] = {100.0};
  assert(NEAR(spv3_rvol(v, 1), 1.0, 1e-12));
}

static void
test_rvol_neutral_when_mean_zero(void)
{
  double v[] = {0.0, 0.0, 500.0};
  assert(NEAR(spv3_rvol(v, 3), 1.0, 1e-12));
}

// -----------------------------------------------------------------------
// spv3_score
// -----------------------------------------------------------------------

static void
test_score_basic(void)
{
  // mom=0.05, rvol=4.0, weight=0.5 → score = 0.05 * 2.0 = 0.10
  assert(NEAR(spv3_score(0.05, 4.0, 0.5), 0.10, 1e-9));
}

static void
test_score_preserves_sign(void)
{
  assert(spv3_score(-0.05, 4.0, 0.5) < 0.0);
}

static void
test_score_zero_weight(void)
{
  // rvol exponent=0 → factor=1 → score = mom
  assert(NEAR(spv3_score(0.03, 100.0, 0.0), 0.03, 1e-9));
}

static void
test_score_nan_mom_propagates(void)
{
  assert(isnan(spv3_score(SPV3_NAN, 2.0, 0.5)));
}

static void
test_score_negative_rvol_treated_as_zero(void)
{
  // rvol < 0 → treated as 0 → pow(0, 0.5)=0 → score=0
  assert(NEAR(spv3_score(0.05, -1.0, 0.5), 0.0, 1e-9));
}

// -----------------------------------------------------------------------
// spv3_gate_open
// -----------------------------------------------------------------------

static void
test_gate_open_passes(void)
{
  spv3_gate_params_t g = {.min_score=0.005, .min_rvol=1.5, .min_mom=0.003};
  assert(spv3_gate_open(0.01, 2.0, 0.005, &g));
}

static void
test_gate_open_fails_score(void)
{
  spv3_gate_params_t g = {.min_score=0.005, .min_rvol=1.5, .min_mom=0.003};
  assert(!spv3_gate_open(0.001, 2.0, 0.005, &g));
}

static void
test_gate_open_fails_rvol(void)
{
  spv3_gate_params_t g = {.min_score=0.005, .min_rvol=1.5, .min_mom=0.003};
  assert(!spv3_gate_open(0.01, 1.0, 0.005, &g));
}

static void
test_gate_open_fails_mom(void)
{
  spv3_gate_params_t g = {.min_score=0.005, .min_rvol=1.5, .min_mom=0.003};
  assert(!spv3_gate_open(0.01, 2.0, 0.001, &g));
}

static void
test_gate_open_rejects_null(void)
{
  assert(!spv3_gate_open(0.01, 2.0, 0.01, NULL));
}

static void
test_gate_open_rejects_nan(void)
{
  spv3_gate_params_t g = {.min_score=0.005, .min_rvol=1.5, .min_mom=0.003};
  assert(!spv3_gate_open(SPV3_NAN, 2.0, 0.01, &g));
}

// -----------------------------------------------------------------------
// spv3_top_n
// -----------------------------------------------------------------------

static void
test_top_n_basic(void)
{
  double scores[] = {0.01, 0.05, 0.03, 0.08, 0.02};
  uint32_t idx[3];
  uint32_t cnt = spv3_top_n(scores, 5, 3, idx);
  assert(cnt == 3);
  assert(idx[0] == 3);  // 0.08
  assert(idx[1] == 1);  // 0.05
  assert(idx[2] == 2);  // 0.03
}

static void
test_top_n_skips_nan(void)
{
  double scores[] = {SPV3_NAN, 0.05, SPV3_NAN, 0.08};
  uint32_t idx[4];
  uint32_t cnt = spv3_top_n(scores, 4, 4, idx);
  assert(cnt == 2);
  assert(idx[0] == 3);
  assert(idx[1] == 1);
}

static void
test_top_n_fewer_than_requested(void)
{
  double scores[] = {0.01, 0.02};
  uint32_t idx[5];
  uint32_t cnt = spv3_top_n(scores, 2, 5, idx);
  assert(cnt == 2);
}

static void
test_top_n_rejects_null(void)
{
  double scores[] = {0.01};
  assert(spv3_top_n(NULL, 1, 1, (uint32_t[]){0}) == 0);
  assert(spv3_top_n(scores, 1, 1, NULL) == 0);
}

// -----------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------

int
main(void)
{
  test_mom_n_basic_positive();
  test_mom_n_basic_negative();
  test_mom_n_flat();
  test_mom_n_rejects_null();
  test_mom_n_rejects_n_less_than_2();
  test_mom_n_rejects_zero_price();
  test_mom_n_uses_first_and_last_only();

  test_rvol_basic();
  test_rvol_below_one();
  test_rvol_neutral_when_null();
  test_rvol_neutral_when_n_less_than_2();
  test_rvol_neutral_when_mean_zero();

  test_score_basic();
  test_score_preserves_sign();
  test_score_zero_weight();
  test_score_nan_mom_propagates();
  test_score_negative_rvol_treated_as_zero();

  test_gate_open_passes();
  test_gate_open_fails_score();
  test_gate_open_fails_rvol();
  test_gate_open_fails_mom();
  test_gate_open_rejects_null();
  test_gate_open_rejects_nan();

  test_top_n_basic();
  test_top_n_skips_nan();
  test_top_n_fewer_than_requested();
  test_top_n_rejects_null();

  puts("All score_pump_v3 scoring tests passed.");
  return(0);
}
