rye_main = files([
	'credits.cpp',
	'main.cpp',
])

libraw_dep = libraw_subprj.get_variable('libraw_dep')

rye_lib = static_library(
	'rye_lib',
	[
		'rye.cpp',
	],
	include_directories: [
		'.',
		'../include',
	],
	dependencies: [
		lak_dep,
		libraw_dep,
	],
)

rye_lib_dep = declare_dependency(
	link_with: rye_lib,
	include_directories: '../include',
	dependencies: [
		lak_dep,
		libraw_dep,
	],
)
