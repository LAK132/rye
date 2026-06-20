project(
	'libraw',
	'cpp',
	license: 'CDDL-1.0 OR LGPL-2.1-only',
	license_files: ['LICENSE.CDDL', 'LICENSE.LGPL'],
	default_options: {
		'c_std': 'c11',
		'cpp_std': 'c++14',
		'warning_level': '0',
		'werror': false,
	},
)

cxx = meson.get_compiler('cpp')

if cxx.get_id() == 'msvc'
	c_cpp_args = [
		'-DUNICODE',
		'-DWIN32_LEAN_AND_MEAN',
		'-DNOMINMAX',
		'-EHsc',
		'-MP',
		'-DWIN32',
	]

	cpp_args = []

	c_args = []
else
	c_cpp_args = []

	cpp_args = []

	c_args = []
endif

c_cpp_args += [
	'-D_CRT_SECURE_NO_WARNINGS',
]

if get_option('libraw_static_library')
	c_cpp_args = [ '-DLIBRAW_NODLL' ]
else
	c_cpp_args = [ '-DLIBRAW_BUILDLIB' ]
endif

if get_option('buildtype') != 'debug'
	c_cpp_args += [ '-DNDEBUG' ]
endif

add_project_arguments(c_cpp_args + cpp_args, language: ['cpp'])
add_project_arguments(c_cpp_args + c_args, language: ['c'])

subdir('src')

meson.override_dependency('libraw', libraw_dep)
