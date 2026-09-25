#ifndef RYE_IMAGE_DATA_HPP
#define RYE_IMAGE_DATA_HPP

#include <lak/col/col.hpp>
#include <lak/col/srgb.hpp>
#include <lak/future.hpp>
#include <lak/image.hpp>
#include <lak/vec.hpp>

#include "libraw_format.hpp"
#include "rye.hpp"

namespace rye
{
	struct image_data
	{
		lak::image<lak::vec3f_t> data;

		lak::mat3f_t cam_to_sRGB       = lak::diagonal(lak::vec3f_t(1.f));
		lak::mat3f_t cam_to_XYZ        = lak::col::sRGB_primaries.linear_to_XYZ();
		lak::mat3f_t XYZ_to_cam        = lak::col::sRGB_primaries.XYZ_to_linear();
		lak::vec3f_t whitebalance_coef = lak::vec3f_t(1.f);

		float iso          = 0.f;
		float shutter      = 0.f;
		float aperture     = 0.f;
		float focal_length = 0.f;

		struct device_info
		{
			lak::u8string make;
			lak::u8string model;
			lak::u8string serial;
		};

		rye::sensor_format_t sensor;
		device_info camera;
		device_info lens;
	};

	lak::future<lak::result<rye::image_data, lak::u8string>> load_image_async(
	  const std::filesystem::path &path);

	lak::result<rye::image_data, lak::u8string> load_image(
	  std::filesystem::path path);

	lak::result<rye::image_data, lak::u8string> load_libraw(
	  std::filesystem::path path);
}

#endif
