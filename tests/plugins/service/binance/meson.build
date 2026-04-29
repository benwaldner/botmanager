test_ws_market_data = executable('test_binance_ws_market_data',
  'test_ws_market_data.c',
  '../../../../plugins/service/binance/ws_market_data.c',
  include_directories : [inc_include, include_directories('../../../../plugins/service/binance')],
  dependencies : [json_c_dep, m_dep],
)

test_bar_cache = executable('test_binance_bar_cache',
  'test_bar_cache.c',
  '../../../../plugins/service/binance/bar_cache.c',
  include_directories : [inc_include, include_directories('../../../../plugins/service/binance')],
  dependencies : [m_dep],
)

test_subscriptions = executable('test_binance_subscriptions',
  'test_subscriptions.c',
  '../../../../plugins/service/binance/subscriptions.c',
  '../../../../plugins/service/binance/ws_market_data.c',
  include_directories : [inc_include, include_directories('../../../../plugins/service/binance')],
  dependencies : [json_c_dep, m_dep],
)

test_dispatch = executable('test_binance_market_data_dispatch',
  'test_market_data_dispatch.c',
  '../../../../plugins/service/binance/market_data_dispatch.c',
  '../../../../plugins/service/binance/subscriptions.c',
  '../../../../plugins/service/binance/bar_cache.c',
  '../../../../plugins/service/binance/ws_market_data.c',
  include_directories : [inc_include, include_directories('../../../../plugins/service/binance')],
  dependencies : [json_c_dep, m_dep],
)

test_backoff = executable('test_binance_ws_backoff',
  'test_ws_backoff.c',
  '../../../../plugins/service/binance/ws_backoff.c',
  include_directories : [inc_include, include_directories('../../../../plugins/service/binance')],
  dependencies : [m_dep],
)

test('binance_ws_market_data', test_ws_market_data)
test('binance_bar_cache', test_bar_cache)
test('binance_subscriptions', test_subscriptions)
test('binance_market_data_dispatch', test_dispatch)
test('binance_ws_backoff', test_backoff)
