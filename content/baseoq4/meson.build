if build_games and build_game_sp
  if host_system == 'windows'
    shared_module(
      game_sp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      vs_module_defs: game_module_defs_file,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  elif host_system == 'darwin'
    shared_library(
      game_sp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args + ['-Wl,-install_name,@loader_path/' + game_sp_binary_name + '.dylib'],
      name_prefix: '',
      name_suffix: 'dylib',
      install: true,
      install_dir: install_game_dir,
    )
  else
    shared_module(
      game_sp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args,
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  endif
endif

if build_games and build_game_mp
  if host_system == 'windows'
    shared_module(
      game_mp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      vs_module_defs: game_module_defs_file,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  elif host_system == 'darwin'
    shared_library(
      game_mp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args + ['-Wl,-install_name,@loader_path/' + game_mp_binary_name + '.dylib'],
      name_prefix: '',
      name_suffix: 'dylib',
      install: true,
      install_dir: install_game_dir,
    )
  else
    shared_module(
      game_mp_binary_name,
      game_sources,
      include_directories: game_include_dirs,
      link_with: game_idlib_library,
      cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
      c_args: game_common_c_args,
      cpp_pch: game_module_pch_headers,
      link_args: game_module_link_args,
      name_prefix: '',
      install: true,
      install_dir: install_game_dir,
    )
  endif
endif

baseoq4_manifest_conf = configuration_data()
baseoq4_manifest_conf.set('OPENQ4_VERSION_BASE', openq4_version_base)
baseoq4_manifest = configure_file(
  input: 'mod.json.in',
  output: 'mod.json',
  configuration: baseoq4_manifest_conf,
)

install_data(
  baseoq4_manifest,
  install_dir: install_game_dir,
)
