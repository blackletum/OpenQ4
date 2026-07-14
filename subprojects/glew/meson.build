project(
  'glew',
  'c',
  default_options: ['default_library=static'],
)

glew_inc = include_directories('include')

glew_c_args = ['-DGLEW_NO_GLU']
if get_option('openq4_sdl3_loader')
  glew_c_args += ['-DOPENQ4_GLEW_SDL3_LOADER']
endif

glew_lib = static_library(
  'glew',
  files('src/glew.c'),
  include_directories: glew_inc,
  c_args: glew_c_args,
)

glew_dep = declare_dependency(
  include_directories: glew_inc,
  link_with: glew_lib,
)

glew_dedicated_dep = disabler()
if host_machine.system() == 'linux'
  # Dedicated Linux targets have no GLX or SDL runtime. Provide a small static
  # GLEW variant that resolves through openQ4's target-owned hook; the Linux
  # dedicated GL stub implements that hook without loading graphics code.
  glew_dedicated_lib = static_library(
    'glew_dedicated',
    files('src/glew.c'),
    include_directories: glew_inc,
    c_args: ['-DGLEW_NO_GLU', '-DOPENQ4_GLEW_SDL3_LOADER'],
  )

  glew_dedicated_dep = declare_dependency(
    include_directories: glew_inc,
    link_with: glew_dedicated_lib,
  )
endif
