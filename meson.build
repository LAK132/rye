project(
	'rye',
	['c', 'cpp'],
	default_options: [
		'b_vscrt=static_from_buildtype',
	],
)

lak_subprj = subproject('lak')

c_cpp_args = lak_subprj.get_variable('c_cpp_args')
cpp_args = lak_subprj.get_variable('cpp_args')
c_args = lak_subprj.get_variable('c_args')
version = lak_subprj.get_variable('version')
lak_dep = lak_subprj.get_variable('lak_dep')

add_project_arguments(c_cpp_args + cpp_args, language: ['cpp'])
add_project_arguments(c_cpp_args + c_args, language: ['c'])

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
	override_options: [
		'cpp_std=' + version,
		'warning_level=3',
		'werror=true',
	],
	dependencies: [
		rye_lib_dep,
	],
)
