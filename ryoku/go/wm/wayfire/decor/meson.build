project(
  'ryoku-decor',
  'cpp',
  version: '1.0.0',
  default_options: [
    'cpp_std=c++20',
    'warning_level=3',
  ],
)

wayfire_dep = dependency('wayfire', version: '>=0.11.0')
gles_dep = dependency('glesv2')
egl_dep = dependency('egl')

sources = files(
  'src/options.cpp',
  'src/plugin.cpp',
  'src/shader.cpp',
  'src/transformer.cpp',
)

shared_module(
  'ryoku-decor',
  sources,
  dependencies: [wayfire_dep, gles_dep, egl_dep],
  name_prefix: 'lib',
  install: true,
  install_dir: get_option('libdir') / 'wayfire',
)

install_data(
  'metadata/ryoku-decor.xml',
  install_dir: get_option('datadir') / 'wayfire' / 'metadata',
)
