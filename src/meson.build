rye_main = files('main.cpp')

rye_lib = static_library(
	'rye_lib',
	[
		'rye.cpp',
	],
	override_options: [
		'cpp_std=' + version,
		'warning_level=3',
		'werror=true',
	],
	include_directories: [
		'.',
		'../include',
	],
	dependencies: [
		lak_dep,
	],
)

rye_lib_dep = declare_dependency(
	link_with: rye_lib,
	include_directories: '../include',
	dependencies: lak_dep,
)
