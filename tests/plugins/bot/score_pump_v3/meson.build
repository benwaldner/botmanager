test_score_pump_v3 = executable('test_score_pump_v3_scoring',
  'test_scoring.c',
  '../../../../plugins/bot/score_pump_v3/score_pump_v3_score.c',
  include_directories : [inc_include, include_directories('../../../../plugins/bot/score_pump_v3')],
  dependencies : [m_dep],
)

test('score_pump_v3_scoring', test_score_pump_v3)
