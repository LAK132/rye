#include "image_data.hpp"
#include "rye.hpp"

#include <lak/binary_reader.hpp>
#include <lak/col/cie.hpp>
#include <lak/col/srgb.hpp>
#include <lak/errors.hpp>
#include <lak/file/x3f.hpp>
#include <lak/format.hpp>
#include <lak/math.hpp>
#include <lak/string_literals/string.hpp>
#include <lak/system/file.hpp>

lak::result<rye::image_data, lak::u8string> rye::load_x3f(
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
	  auto x3f =,
	  strm.template read<lak::x3f::x3f>().map_err(
	    [&](const auto &err)
	    {
		    return lak::fmt<u8"Failed to read {}: {}">(
		      lak::string_view(path.u8string()),
		      err.visit([](const auto &err) { return lak::fmt<u8"{}">(err); }));
	    }));

	uint16_t linear_max           = (1U << 14U) - 1U;
	lak::vec2s_t out_size         = lak::vec2s_t(0);
	rye::image_flip_t orientation = rye::image_flip_t::none;

	x3f.header.versioned.visit(
	  [&](const auto &header)
	  {
		  out_size.x  = header.image_columns;
		  out_size.y  = header.image_rows;
		  orientation = static_cast<rye::image_flip_t>(header.rotation);
	  });

	rye::image_data result;

	lak::tasks tasks{lak::tasks::hardware_max()};

	result.data.resize(out_size);

	size_t entry = 0U;
	for (; entry < x3f.image_entries.size(); ++entry)
		if (x3f.image_entries[entry].versioned.visit(
		      [](const auto &data)
		      {
			      return (data.type == lak::x3f::image_type::One ||
			              data.type == lak::x3f::image_type::Three) &&
			             (data.format == lak::x3f::image_format::x530 ||
			              data.format == lak::x3f::image_format::SD9_SD10_SD14 ||
			              data.format ==
			                lak::x3f::image_format::DP1_DP1S_DP2_Merril ||
			              data.format == lak::x3f::image_format::DP2_Quattro ||
			              data.format == lak::x3f::image_format::SD_Quattro ||
			              data.format == lak::x3f::image_format::SD_Quattro_H ||
			              data.format == lak::x3f::image_format::SD_Quattro_H2);
		      }))
			break;
	if (entry >= x3f.image_entries.size())
		return lak::err_t{lak::fmt<u8"Missing raw entry">()};

	lak::x3f::image_format format = x3f.image_entries[entry].versioned.visit(
	  [&](const auto &data) { return data.format; });

	if (format == lak::x3f::image_format::SD9_SD10_SD14)
		return lak::err_t{
		  lak::fmt<u8"SD9/SD10/SD14 raws are currently not supported">()};

	for (size_t y = 0U; y < result.data.size().y; ++y)
		for (size_t x = 0U; x < result.data.size().x; ++x)
			result.data[{x, y}] = lak::vec3f_t(
			  lak::int_to_frac<float>(x3f.image_entries[entry].image[{x, y}].x,
			                          linear_max),
			  lak::int_to_frac<float>(x3f.image_entries[entry].image[{x, y}].y,
			                          linear_max),
			  lak::int_to_frac<float>(x3f.image_entries[entry].image[{x, y}].z,
			                          linear_max));

	if (orientation != rye::image_flip_t::none)
		result.data = rye::transform(tasks, result.data, orientation);

	result.cam_to_sRGB       = lak::diagonal(lak::vec3f_t(1.f));
	result.cam_to_XYZ        = lak::col::sRGB_primaries.linear_to_XYZ();
	result.XYZ_to_cam        = lak::col::sRGB_primaries.XYZ_to_linear();
	result.whitebalance_coef = lak::vec3f_t(1.f);

	result.sensor = rye::sensor_format_t::foveon;

	// result.iso          = ;
	// result.shutter      = ;
	// result.aperture     = ;
	// result.focal_length = ;

	result.camera.make   = u8""_str;
	result.camera.model  = u8""_str;
	result.camera.serial = u8""_str;

	result.lens.make   = u8""_str;
	result.lens.model  = u8""_str;
	result.lens.serial = u8""_str;

	return lak::move_ok(result);
}
