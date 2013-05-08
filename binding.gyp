{
  'targets': [
    {
      'target_name': 'gmp',
      'sources': [ 'node_gmp.cc' ],
      'cflags!': [ '-fno-exceptions','-Wmissing-braces', '-Wmissing-field-initializers' ],
      'cflags_cc!': [ '-fno-exceptions','-Wmissing-braces', '-Wmissing-field-initializers' ],
      'conditions': [
        ['OS=="linux"',
          {
            'link_settings': {
              'libraries': [
                '-lgmp'
              ]
            }
          }
        ],
        ['OS=="mac"',
          {
            'link_settings': {
              'libraries': [
                '-lgmp'
              ]
            },
            'xcode_settings': {
              'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
            }
          }
        ],
        ['OS=="win"',
          {
            'link_settings': {
              'libraries': [
                '-lgmp.lib'
              ],
            }
          }
        ]
      ]
    }
  ]
}