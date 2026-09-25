#include "image_data.hpp"

#include <lak/binary_reader.hpp>
#include <lak/file/mdc.hpp>
#include <lak/math.hpp>
#include <lak/string_literals/string.hpp>
#include <lak/system/file.hpp>

lak::result<rye::image_data, lak::u8string> rye::load_mdc(
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

	RES_TRY_ASSIGN(auto mdc =,
	               strm.template read<lak::mdc::mdc>().map_err(
	                 [&](const auto &err)
	                 {
		                 return lak::fmt<u8"Failed to read {}: {}">(
		                   lak::string_view(path.u8string()), err);
	                 }));

	rye::image_data result;

	result.data = mdc.process_vec3f();

	result.cam_to_sRGB       = lak::diagonal(lak::vec3f_t(1.f));
	result.cam_to_XYZ        = lak::col::sRGB_primaries.linear_to_XYZ();
	result.XYZ_to_cam        = lak::col::sRGB_primaries.XYZ_to_linear();
	result.whitebalance_coef = lak::vec3f_t(1.f);

	result.iso          = 800.f;
	result.shutter      = mdc.settings.shutter_speed;
	result.aperture     = mdc.settings.aperture;
	result.focal_length = mdc.settings.focal_length;

	result.sensor = rye::sensor_format_t::rd175;

	result.camera.make   = u8"Minolta"_str;
	result.camera.model  = u8"RD-175"_str;
	result.camera.serial = {};

	result.lens.make   = {};
	result.lens.model  = {};
	result.lens.serial = {};

	return lak::move_ok(result);
}
