project('imgio', ['c', 'cpp'],
	version: '0.1.0',
	default_options: [
		'werror=true',
		'warning_level=3',
		'cpp_std=c++17',
		'default_library=static',
	]
)

dep_dlg = dependency('dlg',
	static: true,
	fallback: ['dlg', 'dlg_dep'])
dep_nytl = dependency('nytl',
	fallback: ['nytl', 'nytl_dep'])

deps = [dep_dlg, dep_nytl]
inc = include_directories('include')

args = []

src = files(
	'src/imgio/stream.cpp',
	'src/imgio/file.cpp',
	'src/imgio/image.cpp',
	'src/imgio/ktx2.cpp',
)

lib_imgio = library(
	'imgio',
	sources: [src],
	dependencies: deps,
	include_directories: inc,
	cpp_args: args,
	install: true,
)
