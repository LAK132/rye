includes = include_directories('..')

libraw_decoders = static_library(
	'libraw-decoders',
	[
		'decoders'/'canon_600.cpp',
		'decoders'/'crx.cpp',
		'decoders'/'decoders_dcraw.cpp',
		'decoders'/'decoders_libraw_dcrdefs.cpp',
		'decoders'/'decoders_libraw.cpp',
		'decoders'/'dng.cpp',
		'decoders'/'fp_dng.cpp',
		'decoders'/'fuji_compressed.cpp',
		'decoders'/'generic.cpp',
		'decoders'/'kodak_decoders.cpp',
		'decoders'/'load_mfbacks.cpp',
		'decoders'/'olympus14.cpp',
		'decoders'/'pana8.cpp',
		'decoders'/'smal.cpp',
		'decoders'/'sonycc.cpp',
		'decoders'/'unpack_thumb.cpp',
		'decoders'/'unpack.cpp',
	],
	include_directories: includes,
)

libraw_decompressors = static_library(
	'libraw-decompressors',
	[
		'decompressors'/'losslessjpeg.cpp',
	],
	include_directories: includes,
)

libraw_demosaic = static_library(
	'libraw-demosaic',
	[
		'demosaic'/'aahd_demosaic.cpp',
		'demosaic'/'ahd_demosaic.cpp',
		'demosaic'/'dcb_demosaic.cpp',
		'demosaic'/'dht_demosaic.cpp',
		'demosaic'/'misc_demosaic.cpp',
		'demosaic'/'xtrans_demosaic.cpp',
	],
	include_directories: includes,
)

libraw_integration = static_library(
	'libraw-integration',
	[
		'integration'/'dngsdk_glue.cpp',
		'integration'/'rawspeed_glue.cpp',
	],
	include_directories: includes,
)

libraw_metadata = static_library(
	'libraw-metadata',
	[
		'metadata'/'adobepano.cpp',
		'metadata'/'canon.cpp',
		'metadata'/'ciff.cpp',
		'metadata'/'cr3_parser.cpp',
		'metadata'/'epson.cpp',
		'metadata'/'exif_gps.cpp',
		'metadata'/'fuji.cpp',
		'metadata'/'hasselblad_model.cpp',
		'metadata'/'identify_tools.cpp',
		'metadata'/'identify.cpp',
		'metadata'/'kodak.cpp',
		'metadata'/'leica.cpp',
		'metadata'/'makernotes.cpp',
		'metadata'/'mediumformat.cpp',
		'metadata'/'minolta.cpp',
		'metadata'/'misc_parsers.cpp',
		'metadata'/'nikon.cpp',
		'metadata'/'normalize_model.cpp',
		'metadata'/'olympus.cpp',
		'metadata'/'p1.cpp',
		'metadata'/'pentax.cpp',
		'metadata'/'samsung.cpp',
		'metadata'/'sony.cpp',
		'metadata'/'tiff.cpp',
	],
	include_directories: includes,
)

libraw_postprocessing = static_library(
	'libraw-postprocessing',
	[
		'postprocessing'/'aspect_ratio.cpp',
		'postprocessing'/'dcraw_process.cpp',
		'postprocessing'/'mem_image.cpp',
		'postprocessing'/'postprocessing_aux.cpp',
		# 'postprocessing'/'postprocessing_ph.cpp',
		'postprocessing'/'postprocessing_utils_dcrdefs.cpp',
		'postprocessing'/'postprocessing_utils.cpp',
	],
	include_directories: includes,
)

libraw_preprocessing = static_library(
	'libraw-preprocessing',
	[
		'preprocessing'/'ext_preprocess.cpp',
		# 'preprocessing'/'preprocessing_ph.cpp',
		'preprocessing'/'raw2image.cpp',
		'preprocessing'/'subtract_black.cpp',
	],
	include_directories: includes,
)

libraw_tables = static_library(
	'libraw-tables',
	[
		'tables'/'cameralist.cpp',
		'tables'/'colorconst.cpp',
		'tables'/'colordata.cpp',
		'tables'/'wblists.cpp',
	],
	include_directories: includes,
)

libraw_utils = static_library(
	'libraw-utils',
	[
		'utils'/'curves.cpp',
		'utils'/'decoder_info.cpp',
		'utils'/'init_close_utils.cpp',
		'utils'/'open.cpp',
		'utils'/'phaseone_processing.cpp',
		'utils'/'read_utils.cpp',
		'utils'/'thumb_utils.cpp',
		'utils'/'utils_dcraw.cpp',
		'utils'/'utils_libraw.cpp',
	],
	include_directories: includes,
)

libraw_write = static_library(
	'libraw-write',
	[
		'write'/'apply_profile.cpp',
		'write'/'file_write.cpp',
		'write'/'tiff_writer.cpp',
		# 'write'/'write_ph.cpp',
	],
	include_directories: includes,
)

libraw_x3f = static_library(
	'libraw-x3f',
	[
		'x3f'/'x3f_parse_process.cpp',
		'x3f'/'x3f_utils_patched.cpp',
	],
	include_directories: includes,
)

if get_option('libraw_static_library')
	libraw = static_library(
		'libraw',
		[
			'libraw_c_api.cpp',
			'libraw_datastream.cpp',
		],
		link_with: [
			libraw_decoders,
			libraw_decompressors,
			libraw_demosaic,
			libraw_integration,
			libraw_metadata,
			libraw_postprocessing,
			libraw_preprocessing,
			libraw_tables,
			libraw_utils,
			libraw_write,
			libraw_x3f,
		],
		include_directories: includes,
	)
else
	libraw = shared_library(
		'libraw',
		[
			'libraw_c_api.cpp',
			'libraw_datastream.cpp',
		],
		link_with: [
			libraw_decoders,
			libraw_decompressors,
			libraw_demosaic,
			libraw_integration,
			libraw_metadata,
			libraw_postprocessing,
			libraw_preprocessing,
			libraw_tables,
			libraw_utils,
			libraw_write,
			libraw_x3f,
		],
		include_directories: includes,
	)
endif

libraw_dep = declare_dependency(
	link_with: libraw,
	include_directories: includes,
	version: meson.project_version(),
	compile_args: c_cpp_args,
)
