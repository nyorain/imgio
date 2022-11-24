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

# required for exr, ktx compression support
dep_zlib = dependency('zlib', fallback: ['zlib', 'zlib_dep'])
dep_png = dependency('libpng', fallback: ['png', 'png_dep'], required: false)

dep_turbojpeg = dependency('libturbojpeg', required: false)
dep_webp = dependency('libwebp', required: false)

# todo: make option
with_exr = true

cd = configuration_data()
cd.set('IMGIO_LINUX', build_machine.system() == 'linux')
cd.set('IMGIO_WITH_LIBPNG', dep_png.found())
cd.set('IMGIO_WITH_TURBOJPEG', dep_turbojpeg.found())
cd.set('IMGIO_WITH_WEBP', dep_webp.found())
cd.set('IMGIO_WITH_EXR', with_exr)
subdir('include/imgio')

deps = [
	dep_dlg,
	dep_nytl,
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
	'src/imgio/ktx.cpp',
	'src/imgio/ktx2.cpp',
	'src/imgio/f16.cpp',
	'src/imgio/format.cpp',
)

# libpng
if dep_png.found()
	src += files('src/imgio/png.cpp')
	deps += [dep_png]
endif

# exr
if with_exr
	src += files('src/imgio/exr.cpp')
endif

# webp
if dep_webp.found()
	src += files('src/imgio/webp.cpp')
	deps += [dep_webp]
endif

# turbojpeg
if dep_turbojpeg.found()
	src += files('src/imgio/turbojpeg.cpp')
	deps += [dep_turbojpeg]
endif

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
