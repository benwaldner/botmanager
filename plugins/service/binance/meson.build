bnb_include = include_directories('.')

binance_plugin = shared_library('binance',
  'binance.c',
  'ws_market_data.c',
  'bar_cache.c',
  'subscriptions.c',
  'market_data_dispatch.c',
  'ws_backoff.c',
  include_directories : [inc_include, bnb_include],
  dependencies : [json_c_dep],
  override_options : ['b_lundef=false'],
  install : true,
)
