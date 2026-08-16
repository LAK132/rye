project(
	'rye',
	['c', 'cpp'],
	default_options: {
		'c_std': 'c11',
		'cpp_std': ['c++26', 'c++23', 'c++latest'],
		'b_vscrt': 'static_from_buildtype',
		'warning_level': '2',
		'werror': false,
		'backend_startup_project': 'rye',
		'backend_max_links': 1,
		'prefix': meson.global_build_root()/'install',
		'bindir': 'bin',
		'libdir': 'lib',
		'licensedir': 'share'/'licenses',
	},
)

cxx = meson.get_compiler('cpp')

if cxx.get_id() == 'msvc'
	add_project_arguments(
		'-DUNICODE',
		'-DWIN32_LEAN_AND_MEAN',
		'-DNOMINMAX',
		'-wd4315',
		'-wd4366',
		'-wd4250',
		'-Wv:18',
		'-bigobj',
		language: ['c', 'cpp'])

	add_project_arguments(
		'-Zc:__cplusplus',
		'-Zc:rvalueCast',
		'-Zc:wchar_t',
		'-Zc:ternary',
		'-Zc:preprocessor',
		'-Zc:u8EscapeEncoding',
		language: ['cpp'])

	add_project_arguments(
		'-W0',
		language: ['c'])

	add_project_link_arguments(
		'Winmm.lib',
		'Setupapi.lib',
		'Version.lib',
		'Imm32.lib',
		language: ['c', 'cpp'])
else
	add_project_arguments(
		'-Wno-unknown-warning-option',
		'-Wno-gnu-zero-variadic-macro-arguments',
		'-Wno-language-extension-token',
		'-Wno-dangling-else',
		'-Wno-microsoft-cast',
		'-Wno-pragma-pack',
		'-Wno-deprecated-declarations',
		'-Wno-unused-private-field',
		'-Wno-unused-variable',
		'-Wno-init-list-lifetime',
		'-Wno-invalid-partial-specialization',
		'-Wno-missing-field-initializers',
		'-Wno-maybe-uninitialized',
		'-Wfatal-errors',
		'-Wno-psabi',
		# '-mavx',
		language: ['c', 'cpp'])

	if cxx.get_id() == 'gcc'
		if cxx.has_link_argument('-Wl,-lstdc++_libbacktrace')
			# GCC <14
			add_project_link_arguments(
				'-Wl,-lstdc++_libbacktrace',
				language: ['cpp'])
		elif cxx.has_link_argument('-Wl,-lstdc++exp')
			# GCC >=14
			add_project_link_arguments(
				'-Wl,-lstdc++exp',
				language: ['cpp'])
		else
			warning('Unable to determine backtrace library link argument')
		endif
	endif
endif

add_project_arguments(
	'-D_CRT_SECURE_NO_WARNINGS',
	language: ['c', 'cpp'])

if get_option('buildtype') != 'debug'
	add_project_arguments(
		'-DNDEBUG',
		language: ['c', 'cpp'])
endif

lak_subprj = subproject('lak')
libraw_subprj = subproject('libraw')

lak_dep = lak_subprj.get_variable('lak_dep')

if host_machine.system() == 'windows'
  git_header = custom_target(
    'generate_git_file',
    output: 'rye_git.hpp',
    command: ['generate_git_file.bat', '@OUTPUT@'] ,
    build_by_default: true,
  )
else
  git_header = custom_target(
    'generate_git_file',
    output: 'rye_git.hpp',
    command: ['./generate_git_file.sh', '@OUTPUT@'] ,
    build_by_default: true,
  )
endif

subdir('include')
subdir('src')

executable(
	'rye',
	[
		rye_main,
		git_header,
	],
	dependencies: [
		rye_lib_dep,
	],
	build_by_default: true,
	install: true,
	install_rpath: get_option('prefix')/get_option('libdir'),
)
