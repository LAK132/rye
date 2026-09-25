#include "image_data.hpp"

#include "libraw_format.hpp"
#include "rye.hpp"

#include <lak/format.hpp>
#include <lak/system/file.hpp>

static lak::error_code<lak::u8string> libraw_as_result(int code)
{
	if (code == LIBRAW_SUCCESS)
		return lak::ok_t{};
	else
		return lak::err_t{lak::fmt<u8"{}">(static_cast<LibRaw_errors>(code))};
}

lak::result<rye::image_data, lak::u8string> rye::load_libraw(
  std::filesystem::path path)
{
	LibRaw lraw;

	RES_TRY_ASSIGN(auto file =,
	               lak::read_file(path).map_err(
	                 [&](const std::error_code &ec)
	                 {
		                 return lak::fmt<u8"Failed to open {}: {}">(
		                   lak::string_view(path.u8string()), ec);
	                 }));

	RES_TRY(libraw_as_result(lraw.open_buffer(file.begin(), file.size())));
	RES_TRY(libraw_as_result(lraw.unpack()));
	RES_TRY(libraw_as_result(lraw.raw2image()));
	RES_TRY(libraw_as_result(lraw.subtract_black()));
	// RES_TRY(libraw_as_result(lraw.adjust_maximum()));
	if (lraw.imgdata.process_warnings)
		WARNING(LibRaw_warnings(lraw.imgdata.process_warnings));

	lak::tasks tasks{lak::tasks::hardware_max()};

	lak::image<lak::vec3f_t> img;
	{
		unsigned int white_level =
		  std::max<unsigned int>(0, lraw.imgdata.color.maximum);

		if (lraw.imgdata.idata.is_foveon) white_level = (1U << 14U) - 1U;

		img.resize({lraw.imgdata.sizes.iwidth, lraw.imgdata.sizes.iheight});

		// convert raw to float and apply white level adjustment
		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, iy = y * img.size().x]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  const size_t i = iy + x;

					  img[i].r = float(lraw.imgdata.image[i][0]);
					  img[i].g = std::max(float(lraw.imgdata.image[i][1]),
					                      float(lraw.imgdata.image[i][3]));
					  img[i].b = float(lraw.imgdata.image[i][2]);

					  img[i] /= lak::vec3f_t(float(white_level));
				  }
			  });
		}
		tasks.await();
	}

	rye::image_data result;

	if (lraw.imgdata.idata.is_foveon)
	{
		result.sensor = rye::sensor_format_t::foveon;
		result.data   = rye::demosaic_foveon(
      tasks, img, static_cast<rye::image_flip_t>(lraw.imgdata.sizes.flip));
	}
	else if (lraw.imgdata.idata.filters == 9U)
	{
		result.sensor = rye::sensor_format_t::xtrans;
		result.data   = rye::demosaic_xtrans(
      tasks, img, static_cast<rye::image_flip_t>(lraw.imgdata.sizes.flip));
	}
	else
	{
		if (lr.imgdata.idata.maker_index == LIBRAW_CAMERAMAKER_Minolta &&
		    lak::astring_view(lr.imgdata.idata.model) == "RD175"_view)
			result.sensor = rye::sensor_format_t::rd175;
		else
			result.sensor = rye::sensor_format_t::bayer;

		lak::vec2s_t channels[4U] = {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
				if (int col = lraw.COLOR(y, x); col <= 3)
					channels[size_t(col)] = {size_t(x), size_t(y)};
		result.data = rye::demosaic_bayer(
		  tasks,
		  img,
		  lak::span(channels),
		  static_cast<rye::image_flip_t>(lraw.imgdata.sizes.flip));
	}
	tasks.await();

	result.cam_to_sRGB = lak::mat3f_t{
	  lak::vec4f_t(lraw.imgdata.color.rgb_cam[0]).xyz(),
	  lak::vec4f_t(lraw.imgdata.color.rgb_cam[1]).xyz(),
	  lak::vec4f_t(lraw.imgdata.color.rgb_cam[2]).xyz(),
	};
	result.cam_to_XYZ = lak::mat3f_t{
	  lak::vec4f_t(lraw.imgdata.color.ccm[0]).xyz(),
	  lak::vec4f_t(lraw.imgdata.color.ccm[1]).xyz(),
	  lak::vec4f_t(lraw.imgdata.color.ccm[2]).xyz(),
	};
	result.XYZ_to_cam = lak::mat3f_t{
	  lak::vec3f_t(lraw.imgdata.color.cam_xyz[0]),
	  lak::vec3f_t(lraw.imgdata.color.cam_xyz[1]),
	  lak::vec3f_t(lraw.imgdata.color.cam_xyz[2]),
	};
	result.whitebalance_coef = lak::vec4f_t(lraw.imgdata.color.pre_mul).xyz();

	if (bool cam_to_XYZ_valid = !lak::close_to(lak::det(result.cam_to_XYZ), 0.f),
	    XYZ_to_cam_valid      = !lak::close_to(lak::det(result.XYZ_to_cam), 0.f);
	    cam_to_XYZ_valid && XYZ_to_cam_valid)
		;
	else if (cam_to_XYZ_valid && !XYZ_to_cam_valid)
		result.XYZ_to_cam = lak::inverse(result.cam_to_XYZ);
	else if (XYZ_to_cam_valid && !cam_to_XYZ_valid)
		result.cam_to_XYZ = lak::inverse(result.XYZ_to_cam);
	else
	{
		if (lak::close_to(lak::det(result.cam_to_sRGB), 0.f))
		{
			ERROR(
			  "Raw file provides no conversion from "
			  "camera colour space to CIE-XYZ");
			result.cam_to_sRGB = lak::diagonal(lak::vec3f_t(1.f));
		}
		constexpr lak::mat3f_t sRGB_to_XYZ =
		  lak::col::sRGB_primaries.linear_to_XYZ();
		auto cam_to_sRGB_basis = lak::transpose(result.cam_to_sRGB);
		result.cam_to_XYZ      = lak::mat3f_t{
      sRGB_to_XYZ * cam_to_sRGB_basis.x,
      sRGB_to_XYZ * cam_to_sRGB_basis.y,
      sRGB_to_XYZ * cam_to_sRGB_basis.z,
    };
		result.XYZ_to_cam = lak::inverse(result.cam_to_XYZ);
	}

	result.iso          = lraw.imgdata.other.iso_speed;
	result.shutter      = lraw.imgdata.other.shutter;
	result.aperture     = lraw.imgdata.other.aperture;
	result.focal_length = lraw.imgdata.other.focal_len;

	result.camera.make  = lak::to_u8string(lraw.imgdata.idata.make);
	result.camera.model = lak::to_u8string(lraw.imgdata.idata.model);
	result.camera.serial =
	  lak::to_u8string(lraw.imgdata.shootinginfo.BodySerial);

	result.lens.make   = lak::to_u8string(lraw.imgdata.lens.LensMake);
	result.lens.model  = lak::to_u8string(lraw.imgdata.lens.Lens);
	result.lens.serial = lak::to_u8string(lraw.imgdata.lens.LensSerial);

	return lak::move_ok(result);
}
