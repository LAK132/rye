project(
	'rye',
	['c', 'cpp'],
	default_options: [
		'b_vscrt=static_from_buildtype',
	],
)

cxx = meson.get_compiler('cpp')

if cxx.get_id() == 'msvc'
	version = 'c++latest'

	add_project_arguments(
		'-DUNICODE',
		'-DWIN32_LEAN_AND_MEAN',
		'-DNOMINMAX',
		'-wd4315',
		'-wd4366',
		'-wd4250',
		'-Wv:18',
		language: ['c', 'cpp'])

	add_project_arguments(
		'-Zc:__cplusplus',
		'-Zc:rvalueCast',
		'-Zc:wchar_t',
		'-Zc:ternary',
		'-Zc:preprocessor',
		language: ['cpp'])

	add_project_arguments(
		'-W0',
		language: ['c'])

	system_libraries = [
		'Winmm.lib',
		'Setupapi.lib',
		'Version.lib',
		'Imm32.lib',
	]
else
	version = 'c++20'

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

	system_libraries = []
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

add_project_arguments(
	libraw_subprj.get_variable('c_cpp_args'),
	language: ['c', 'cpp'])

version = lak_subprj.get_variable('version')
lak_dep = lak_subprj.get_variable('lak_dep')

if cxx.get_id() == 'msvc'
	add_project_arguments(
		'-bigobj',
		language: ['c', 'cpp'])
endif

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
	override_options: lak_subprj.get_variable('override_options_w2'),
	dependencies: [
		rye_lib_dep,
	],
	install: true,
)
