score_pump_v3_plugin = shared_library('score_pump_v3',
  'score_pump_v3.c',
  'score_pump_v3_score.c',
  include_directories : [inc_include, include_directories('.')],
  dependencies : [m_dep],
  override_options : ['b_lundef=false'],
  install : true,
)
