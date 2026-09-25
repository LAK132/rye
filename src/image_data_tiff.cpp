#include "image_data.hpp"
#include "rye.hpp"

#include <lak/bit_reader.hpp>
#include <lak/col/cie.hpp>
#include <lak/col/srgb.hpp>
#include <lak/errors.hpp>
#include <lak/file/tiff.hpp>
#include <lak/format.hpp>
#include <lak/string_literals/string.hpp>
#include <lak/system/file.hpp>

lak::result<rye::image_data, lak::u8string> rye::load_tiff(
  std::filesystem::path path)
{
	RES_TRY_ASSIGN(auto file =,
	               lak::read_file(path).map_err(
	                 [&](const std::error_code &ec)
	                 {
		                 return lak::fmt<u8"Failed to open {}: {}">(
		                   lak::string_view(path.u8string()), ec);
	                 }));

	lak::binary_reader strm{file};

	RES_TRY_ASSIGN(
	  auto tiff =,
	  strm.template read<lak::tiff::tiff>().map_err(
	    [&](const auto &err)
	    {
		    return lak::fmt<u8"Failed to read {}: {}">(
		      lak::string_view(path.u8string()),
		      err.visit([](const auto &err) { return lak::fmt<u8"{}">(err); }));
	    }));

	DEBUG_EXPR(tiff.ifd.size());

	if (tiff.ifd.empty())
		return lak::err_t{lak::fmt<u8"Failed to read {}: no IFDs">(
		  lak::string_view(path.u8string()))};

	rye::image_data result;

	size_t width            = 0U;
	size_t height           = 0U;
	uint32_t ifd_type       = 0U;
	uint16_t compression    = 0U;
	uint16_t interpretation = 0U;
	uint16_t lightsource    = 0U;
	lak::vec2s_t crop_origin{0, 0};
	lak::vec2s_t crop_size{0, 0};
	lak::vec2u16_t cfa_repeat{0, 0};
	lak::array<uint8_t> cfa_pattern;

	rye::image_flip_t orientation = rye::image_flip_t::none;
	lak::vec3u16_t bits_per_sample{8U, 8U, 8U};
	uint16_t samples_per_pixel = 0U;

	auto real_img_getter = lak::overloaded{
	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::NewSubfileType>,
	      lak::span<const uint32_t> data)
	  {
		  if (data.empty()) return;
		  ifd_type = data[0];
	  },

	  []<lak::tiff::tag_name TAG>(lak::tiff::tag_name_type<TAG>, ...) {},
	};

	auto img_getter = lak::overloaded{
	  [&]<typename T>(lak::tiff::tag_name_type<lak::tiff::tag_name::ImageWidth>,
	                  lak::span<const T> data)
	  {
		  if (data.empty()) return;
		  width = size_t(data[0]);
	  },

	  [&]<typename T>(lak::tiff::tag_name_type<lak::tiff::tag_name::ImageLength>,
	                  lak::span<const T> data)
	  {
		  if (data.empty()) return;
		  height = size_t(data[0]);
	  },

	  [&]<typename T>(
	    lak::tiff::tag_name_type<lak::tiff::tag_name::DefaultCropOrigin>,
	    lak::span<const T> data)
	  {
		  if (data.size() != 2U) return;
		  if constexpr (lak::is_same_v<T, lak::tiff::urational>)
		  {
			  crop_origin.x = data[0].numerator / data[0].denominator;
			  crop_origin.y = data[1].numerator / data[1].denominator;
		  }
		  else
			  crop_origin = lak::vec2s_t(data[0], data[1]);
	  },

	  [&]<typename T>(
	    lak::tiff::tag_name_type<lak::tiff::tag_name::DefaultCropSize>,
	    lak::span<const T> data)
	  {
		  if (data.size() != 2U) return;
		  if constexpr (lak::is_same_v<T, lak::tiff::urational>)
		  {
			  crop_size.x = data[0].numerator / data[0].denominator;
			  crop_size.y = data[1].numerator / data[1].denominator;
		  }
		  else
			  crop_size = lak::vec2s_t(data[0], data[1]);
	  },

	  [&](
	    lak::tiff::tag_name_type<lak::tiff::tag_name::PhotometricInterpretation>,
	    lak::span<const uint16_t> data)
	  {
		  if (data.empty()) return;
		  interpretation = data[0];
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::Compression>,
	      lak::span<const uint16_t> data)
	  {
		  if (data.empty()) return;
		  compression = data[0];
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::CFARepeatPatternDim>,
	      lak::span<const uint16_t> data)
	  {
		  if (data.size() != 2U) return;
		  cfa_repeat = lak::vec2u16_t(data[0], data[1]);
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::CFAPattern>,
	      lak::span<const uint8_t> data)
	  {
		  cfa_pattern.clear();
		  cfa_pattern.push_back(data.begin(), data.end());
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::BitsPerSample>,
	      lak::span<const uint16_t> data)
	  {
		  if (data.size() == 1U)
		  {
			  bits_per_sample.x = data[0];
			  bits_per_sample.y = data[0];
			  bits_per_sample.z = data[0];
		  }
		  else if (data.size() == 3U)
		  {
			  bits_per_sample.x = data[0];
			  bits_per_sample.y = data[1];
			  bits_per_sample.z = data[2];
		  }
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::SamplesPerPixel>,
	      lak::span<const uint16_t> data)
	  {
		  if (data.empty()) return;
		  samples_per_pixel = data[0];
	  },

	  []<lak::tiff::tag_name TAG>(lak::tiff::tag_name_type<TAG>, ...) {},
	};

	auto meta_getter = lak::overloaded{
	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::Orientation>,
	      lak::span<const uint16_t> data)
	  {
		  if (data.empty()) return;
		  switch (data[0])
		  {
			  case 3U:
				  orientation = rye::image_flip_t::reverse_xy;
				  break;
			  case 6U:
				  orientation = rye::image_flip_t::transpose;
				  break;
			  case 8U:
				  orientation =
				    rye::image_flip_t::reverse_xy | rye::image_flip_t::transpose;
				  break;
			  default:
				  break;
		  }
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::ISOSpeedRatings>,
	      lak::span<const lak::tiff::urational> data)
	  {
		  if (data.empty()) return;
		  result.iso = float(data[0].numerator) / float(data[0].denominator);
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::ExposureTime>,
	      lak::span<const lak::tiff::urational> data)
	  {
		  if (data.empty()) return;
		  result.shutter = float(data[0].numerator) / float(data[0].denominator);
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::FNumber>,
	      lak::span<const lak::tiff::urational> data)
	  {
		  if (data.empty()) return;
		  result.aperture = float(data[0].numerator) / float(data[0].denominator);
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::FocalLength>,
	      lak::span<const lak::tiff::urational> data)
	  {
		  if (data.empty()) return;
		  result.focal_length =
		    float(data[0].numerator) / float(data[0].denominator);
	  },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::Make>,
	      lak::span<const char> data)
	  { result.camera.make = lak::to_u8string(data.data(), data.size()); },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::Model>,
	      lak::span<const char> data)
	  { result.camera.model = lak::to_u8string(data.data(), data.size()); },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::UniqueCameraModel>,
	      lak::span<const char> data)
	  { result.camera.model = lak::to_u8string(data.data(), data.size()); },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::CameraSerialNumber>,
	      lak::span<const char> data)
	  { result.camera.serial = lak::to_u8string(data.data(), data.size()); },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::LensMake>,
	      lak::span<const char> data)
	  { result.lens.make = lak::to_u8string(data.data(), data.size()); },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::LensModel>,
	      lak::span<const char> data)
	  { result.lens.model = lak::to_u8string(data.data(), data.size()); },

	  [&](lak::tiff::tag_name_type<lak::tiff::tag_name::LensSerialNumber>,
	      lak::span<const char> data)
	  { result.lens.serial = lak::to_u8string(data.data(), data.size()); },

	  []<lak::tiff::tag_name TAG>(lak::tiff::tag_name_type<TAG>, ...) {},
	};

	if (tiff.ifd[0].exif)
		for (const auto &tag : tiff.ifd[0].exif->tags) tag.visit(meta_getter);
	for (const auto &tag : tiff.ifd[0].tags) tag.visit(meta_getter);

	const lak::tiff::image_file_directory *img_ifd = &tiff.ifd[0];

	for (const auto &tag : tiff.ifd[0].tags) tag.visit(real_img_getter);
	if (ifd_type == 0U)
		for (const auto &tag : tiff.ifd[0].tags) tag.visit(img_getter);
	else
		for (const auto &subifd : tiff.ifd[0].subifds)
		{
			for (const auto &tag : subifd.tags) tag.visit(real_img_getter);
			if (ifd_type == 0U)
			{
				for (const auto &tag : subifd.tags) tag.visit(img_getter);
				for (const auto &tag : subifd.tags) tag.visit(meta_getter);
				img_ifd = &subifd;
				break;
			}
		}
	if (ifd_type != 0U)
		return lak::err_t{u8"Image contains only thumbnails"_str};

	if (!(compression == 0U || compression == 1U))
		return lak::err_t{
		  lak::fmt<u8"Unsupported compression method ({})">(compression)};

	if (!(samples_per_pixel == 1U || samples_per_pixel == 3U))
		return lak::err_t{
		  lak::fmt<u8"Unsupported channel count ({})">(samples_per_pixel)};

	if (width == 0U || height == 0U)
		return lak::err_t{
		  lak::fmt<u8"Invalid image dimensions ({} x {})">(width, height)};

	if (crop_origin.x + crop_size.x > width ||
	    crop_origin.y + crop_size.y > height)
		return lak::err_t{lak::fmt<u8"Invalid crop dimensions ({}+{} > ({}, {}))">(
		  crop_origin, crop_size, width, height)};

	size_t strip_bytes = 0U;
	for (const auto &strip : img_ifd->strips) strip_bytes += strip.data.size();

	lak::bit_reader<lak::endian::little, lak::endian::big> bit_strm;

	lak::tasks tasks{lak::tasks::hardware_max()};

	auto read_channel = [&, strip = size_t(0)](uint8_t bits) mutable
	  -> lak::result<float, lak::u8string>
	{
		if (bit_strm.bytes_remaining() < lak::bit_count::from_bits(bits))
			if (bit_strm.try_accumulate_remaining())
				bit_strm.reset_data(img_ifd->strips[strip++].data);
		RES_TRY_ASSIGN(uintmax_t read =,
		               bit_strm.read_bits(bits).map_err(
		                 [](const auto &err)
		                 {
			                 return err.visit([](const auto &err)
			                                  { return lak::fmt<u8"{}">(err); });
		                 }));
		return lak::ok_t{lak::int_to_frac<float>(
		  read, uintmax_t(UINT32_MAX >> (32U - bits_per_sample.x)))};
	};

	result.data.resize({width, height});

	DEBUG_EXPR(interpretation);
	DEBUG_EXPR(crop_origin);
	DEBUG_EXPR(crop_size);
	DEBUG_EXPR(bits_per_sample);
	DEBUG_EXPR(samples_per_pixel);
	DEBUG_EXPR(cfa_repeat);

	switch (interpretation)
	{
		case 34892U: // DNG LinearRAW
			result.sensor = rye::sensor_format_t::rgb;
			if (samples_per_pixel == 1U)
			{
				case 0U: // Monochrome
					for (size_t y = 0U; y < result.data.size().y; ++y)
					{
						for (size_t x = 0U; x < result.data.size().x; ++x)
						{
							float col;
							RES_TRY_ASSIGN(col =, read_channel(bits_per_sample.x));
							result.data[{x, y}] = lak::vec3f_t(col);
						}
					}
					if (crop_size != lak::vec2s_t(0, 0) &&
					    crop_size != result.data.size())
						result.data =
						  rye::crop(tasks, result.data, crop_origin, crop_size);
					if (orientation != rye::image_flip_t::none)
						result.data = rye::transform(tasks, result.data, orientation);
					break;
			}
			else if (samples_per_pixel == 3U)
			{
				case 6U: // YCbCr
					[[fallthrough]];
				case 1U: // RGB
					for (size_t y = 0U; y < result.data.size().y; ++y)
					{
						for (size_t x = 0U; x < result.data.size().x; ++x)
						{
							lak::vec3f_t col{0, 0, 0};
							RES_TRY_ASSIGN(col.x =, read_channel(bits_per_sample.x));
							RES_TRY_ASSIGN(col.y =, read_channel(bits_per_sample.y));
							RES_TRY_ASSIGN(col.z =, read_channel(bits_per_sample.z));
							result.data[{x, y}] = col;
						}
					}
					if (crop_size != lak::vec2s_t(0, 0) &&
					    crop_size != result.data.size())
						result.data =
						  rye::crop(tasks, result.data, crop_origin, crop_size);
					if (orientation != rye::image_flip_t::none)
						result.data = rye::transform(tasks, result.data, orientation);
					break;
			}
			BOUNDS_ASSERT_UNREACHABLE();
			break;

		case 32803U: // TIFF/EP CFA
		{
			result.sensor = rye::sensor_format_t::bayer;
			if (lak::manhattan(cfa_repeat) != cfa_pattern.size())
				return lak::err_t{
				  lak::fmt<u8"CFA repeat dimensions/pattern size mismatch ({} / {})">(
				    cfa_repeat, cfa_pattern.size())};
			if (cfa_repeat == lak::vec2u16_t(2, 2))
			{
				lak::vec2s_t channels[4U] = {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
				for (size_t y = 0; y < 2; ++y)
					for (size_t x = 0; x < 2; ++x)
						if (uint8_t col = cfa_pattern[(y * cfa_repeat.x) + x]; col <= 3)
							channels[size_t(col)] = {size_t(x), size_t(y)};

				for (size_t y = 0U; y < result.data.size().y; ++y)
				{
					for (size_t x = 0U; x < result.data.size().x; ++x)
					{
						uint8_t c = cfa_pattern[((y % cfa_repeat.y) * cfa_repeat.x) +
						                        (x % cfa_repeat.x)];
						lak::vec3f_t col{0, 0, 0};
						RES_TRY_ASSIGN(col[c] =, read_channel(bits_per_sample[c]));
						result.data[{x, y}] = col;
					}
				}

				if (crop_size != lak::vec2s_t(0, 0) && crop_size != result.data.size())
				{
					// demosaic_bayer causes a ((2, 2), (2, 2)) crop anyway
					auto crop_max = crop_origin + crop_size;

					crop_origin.x = std::max<size_t>(2U, crop_origin.x) - 2U;
					crop_origin.y = std::max<size_t>(2U, crop_origin.y) - 2U;

					crop_max.x = std::min<size_t>(crop_max.x + 2U, result.data.size().x);
					crop_max.y = std::min<size_t>(crop_max.y + 2U, result.data.size().y);

					crop_size = crop_max - crop_origin;

					if (crop_origin != lak::vec2s_t(0, 0) ||
					    crop_max != result.data.size())
						result.data =
						  rye::crop(tasks, result.data, crop_origin, crop_size);
				}

				result.data = rye::demosaic_bayer(
				  tasks, result.data, lak::span(channels), orientation);
			}
			else
				return lak::err_t{lak::fmt<u8"Unknown CFA repeat ({})">(cfa_repeat)};
		}
		break;

		default:
			return lak::err_t{
			  lak::fmt<u8"Unsupported photometric interpretation ({})">(
			    interpretation)};
	}

	auto primaries = lak::col::sRGB_primaries;

	switch (lightsource)
	{
		case 0U: // Unidentified
			break;
		case 1U: // Daylight
			break;
		case 2U: // Fluorescent light
			break;
		case 3U: // Tungsten lamp
			break;
		case 10U: // Flash
			break;
		case 17U: // Standard Illuminant A
			primaries.w = lak::col::cie::A_xy;
			primaries.regenerate_Y();
			break;
		case 18U: // Standard Illuminant B
			primaries.w = lak::col::cie::B_xy;
			primaries.regenerate_Y();
			break;
		case 19U: // Standard Illuminant C
			primaries.w = lak::col::cie::C_xy;
			primaries.regenerate_Y();
			break;
		case 20U: // D55
			primaries.w = lak::col::cie::D55_xy;
			primaries.regenerate_Y();
			break;
		case 21U: // D65
			primaries.w = lak::col::cie::D65_xy;
			primaries.regenerate_Y();
			break;
		case 22U: // D75
			primaries.w = lak::col::cie::D75_xy;
			primaries.regenerate_Y();
			break;
		default:
		{
			if ((lightsource & (1U << 15U)) != 0U)
			{
				uint16_t colour_temp = lightsource & ((1U << 15U) - 1U);
				float cct            = lak::col::cie::D_series_CCT(float(colour_temp));
				if (cct >= 4000.f && cct <= 25000.f)
				{
					primaries.w = lak::col::cie::D_series_illuminant(cct);
					primaries.regenerate_Y();
				}
			}
		}
		break;
	}

	result.cam_to_XYZ = primaries.linear_to_XYZ();
	result.XYZ_to_cam = primaries.XYZ_to_linear();

	constexpr auto XYZ_to_sRGB = lak::col::sRGB_primaries.XYZ_to_linear();

	result.cam_to_sRGB = lak::transpose(lak::mat3f_t{
	  XYZ_to_sRGB * lak::col::cie::to_XYZ(primaries.r).to_vec(),
	  XYZ_to_sRGB * lak::col::cie::to_XYZ(primaries.g).to_vec(),
	  XYZ_to_sRGB * lak::col::cie::to_XYZ(primaries.b).to_vec(),
	});

	result.whitebalance_coef = lak::vec3f_t(1.f);

	return lak::move_ok(result);
}
