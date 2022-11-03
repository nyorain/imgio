project('imgio', ['c', 'cpp'],
	version: '0.1.0',
	default_options: [
		'werror=true',
		'warning_level=3',
		'cpp_std=c++17',
		'default_library=static',
	]
)

cc = meson.get_compiler('cpp')
source_root = '/'.join(meson.source_root().split('\\'))

dep_dlg = dependency('dlg',
	static: true,
	fallback: ['dlg', 'dlg_dep'])
dep_nytl = dependency('nytl',
	fallback: ['nytl', 'nytl_dep'])

dep_png = dependency('libpng', fallback: ['png', 'png_dep'])
dep_zlib = dependency('zlib', fallback: ['zlib', 'zlib_dep']) # for exr support

deps = [
	dep_dlg,
	dep_nytl,
	dep_png,
	dep_zlib,
]
inc = include_directories('include')

common_args = [
	'-Wpedantic',
	'-Wduplicated-cond',
	'-Wrestrict',
	'-Wnull-dereference',
	'-Wundef',
	'-Wlogical-op',

	# for clang
	'-Wno-missing-braces',
	'-Wno-undefined-var-template',

	'-DDLG_BASE_PATH="@0@/"'.format(source_root),

	# Highly useful for debugging on linux.
	'-D_GLIBCXX_DEBUG',

	# msvc
	'-D_CRT_SECURE_NO_WARNINGS',
	'/wd26812', # prefer 'enum class' over 'enum'. Warning isn't wrong but can't change external code
	'/wd4244', # lossy type conversion (e.g. double -> int)
	'/wd4267', # lossy type conversion (e.g. size -> unsigned int)
	'/wd4305', # truncating type conversion (e.g. double -> float)
]

common_args = cc.get_supported_arguments(common_args)

src = files(
	'src/imgio/stream.cpp',
	'src/imgio/file.cpp',
	'src/imgio/image.cpp',
	'src/imgio/png.cpp',
	'src/imgio/ktx.cpp',
	'src/imgio/ktx2.cpp',
	'src/imgio/exr.cpp',
	'src/imgio/f16.cpp',
	'src/imgio/format.cpp',
)

lib_imgio = library(
	'imgio',
	sources: [src],
	dependencies: deps,
	include_directories: inc,
	cpp_args: common_args,
	install: true,
)

imgio_dep = declare_dependency(
	include_directories: inc,
	compile_args: [],
	link_with: [lib_imgio],
	dependencies: deps)
