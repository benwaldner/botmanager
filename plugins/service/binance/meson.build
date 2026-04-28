bnb_include = include_directories('.')

binance_plugin = shared_library('binance',
  'binance.c',
  include_directories : [inc_include, bnb_include],
  override_options : ['b_lundef=false'],
  install : true,
)
