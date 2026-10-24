rye_main = files([
	'credits.cpp',
	'image_proc.cpp',
	'main.cpp',
])

libraw_dep = libraw_subprj.get_variable('libraw_dep')

rye_lib = static_library(
	'rye_lib',
	[
		'image_data_libraw.cpp',
		'image_data_mdc.cpp',
		'image_data_tiff.cpp',
		'image_data_x3f.cpp',
		'image_data.cpp',
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
