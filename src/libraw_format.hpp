#ifndef RYE_LIBRAW_FORMAT_HPP
#define RYE_LIBRAW_FORMAT_HPP

#include <lak/array.hpp>
#include <lak/format.hpp>
#include <lak/stdint.hpp>
#include <lak/string_utils.hpp>

#include <libraw/libraw.h>

template<typename CHAR>
struct lak::format_traits<LibRaw_warnings, CHAR>
{
	static constexpr lak::string<CHAR> to_string(const LibRaw_warnings &warn)
	{
		lak::array<lak::string<CHAR>> warnings;

		auto get_warning_str = [](const LibRaw_warnings &warn) -> lak::string<CHAR>
		{
			switch (warn)
			{
				case LIBRAW_WARN_NONE:
					return lak::strconv<CHAR>("None"_view);
				case LIBRAW_WARN_BAD_CAMERA_WB:
					return lak::strconv<CHAR>("Bad camera white balance"_view);
				case LIBRAW_WARN_NO_METADATA:
					return lak::strconv<CHAR>("No metadata"_view);
				case LIBRAW_WARN_NO_JPEGLIB:
					return lak::strconv<CHAR>("No JPEG lib"_view);
				case LIBRAW_WARN_NO_EMBEDDED_PROFILE:
					return lak::strconv<CHAR>("No embedded profile"_view);
				case LIBRAW_WARN_NO_INPUT_PROFILE:
					return lak::strconv<CHAR>("No input profile"_view);
				case LIBRAW_WARN_BAD_OUTPUT_PROFILE:
					return lak::strconv<CHAR>("Bad output profile"_view);
				case LIBRAW_WARN_NO_BADPIXELMAP:
					return lak::strconv<CHAR>("No badpixelmap"_view);
				case LIBRAW_WARN_BAD_DARKFRAME_FILE:
					return lak::strconv<CHAR>("Bad darkframe file"_view);
				case LIBRAW_WARN_BAD_DARKFRAME_DIM:
					return lak::strconv<CHAR>("Bad farkframe dimension"_view);
				case LIBRAW_WARN_RAWSPEED_PROBLEM:
					return lak::strconv<CHAR>("RawSpeed problem"_view);
				case LIBRAW_WARN_RAWSPEED_UNSUPPORTED:
					return lak::strconv<CHAR>("RawSpeed unsupported"_view);
				case LIBRAW_WARN_RAWSPEED_PROCESSED:
					return lak::strconv<CHAR>("RawSpeed processed"_view);
				case LIBRAW_WARN_FALLBACK_TO_AHD:
					return lak::strconv<CHAR>("Fallback to AHD"_view);
				case LIBRAW_WARN_PARSEFUJI_PROCESSED:
					return lak::strconv<CHAR>("ParseFuji processed"_view);
				case LIBRAW_WARN_DNGSDK_PROCESSED:
					return lak::strconv<CHAR>("DNGSDK processed"_view);
				case LIBRAW_WARN_DNG_IMAGES_REORDERED:
					return lak::strconv<CHAR>("DNG images reordered"_view);
				case LIBRAW_WARN_DNG_STAGE2_APPLIED:
					return lak::strconv<CHAR>("DNG stage 2 applied"_view);
				case LIBRAW_WARN_DNG_STAGE3_APPLIED:
					return lak::strconv<CHAR>("SNG stage 3 applied"_view);
				case LIBRAW_WARN_RAWSPEED3_PROBLEM:
					return lak::strconv<CHAR>("RawSpeed3 problem"_view);
				case LIBRAW_WARN_RAWSPEED3_UNSUPPORTED:
					return lak::strconv<CHAR>("RawSpeed3 unsupported"_view);
				case LIBRAW_WARN_RAWSPEED3_PROCESSED:
					return lak::strconv<CHAR>("RawSpeed3 processed"_view);
				case LIBRAW_WARN_RAWSPEED3_NOTLISTED:
					return lak::strconv<CHAR>("RawSpeeed3 not listed"_view);
				case LIBRAW_WARN_VENDOR_CROP_SUGGESTED:
					return lak::strconv<CHAR>("Vendor crop suggested"_view);
				case LIBRAW_WARN_DNG_NOT_PROCESSED:
					return lak::strconv<CHAR>("DNG not processed"_view);
				case LIBRAW_WARN_DNG_NOT_PARSED:
					return lak::strconv<CHAR>("DNG not parsed"_view);
				default:
					BOUNDS_ASSERT_UNREACHABLE(
					  return lak::strconv<CHAR>("Unknown warning"_view));
			}
		};

		for (uint32_t i = 2; i < 27; ++i)
			if ((warn & LibRaw_warnings(1 << i)) != 0)
				warnings.push_back(
				  get_warning_str(LibRaw_warnings(warn & LibRaw_warnings(1 << i))));

		return lak::join_strings<CHAR>(lak::strconv<CHAR>("|"_view), warnings);
	}
};

template<typename CHAR>
struct lak::format_traits<LibRaw_errors, CHAR>
{
	static constexpr lak::string<CHAR> to_string(const LibRaw_errors &err)
	{
		return lak::strconv<CHAR>(
		  lak::astring_view::from_c_str(libraw_strerror(err)));
	}
};

#endif
