project('victus-control', 'cpp', 'c',
  version: '1.0',
  default_options: ['cpp_std=c++20']
)

# Add compiler warning flags
add_project_arguments('-Wall', '-Wextra', '-Wpedantic', language : 'cpp')

gtkmm_dep = dependency('gtk4')

subdir('backend')
subdir('frontend')