#include "image_data.hpp"
#include "rye.hpp"

#include <lak/binary_reader.hpp>
#include <lak/col/cie.hpp>
#include <lak/col/srgb.hpp>
#include <lak/dsl/utility.hpp>
#include <lak/errors.hpp>
#include <lak/file/tiff.hpp>
#include <lak/file/x3f.hpp>
#include <lak/format.hpp>
#include <lak/math.hpp>
#include <lak/span_manip.hpp>
#include <lak/string_literals/span.hpp>
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
		  out_size.x = header.image_columns;
		  out_size.y = header.image_rows;
		  switch (header.rotation)
		  {
			  case 90:
				  orientation = rye::image_flip_t::transpose;
				  break;
			  case 180:
				  orientation = rye::image_flip_t::reverse_xy;
				  break;
			  case 270:
				  orientation =
				    rye::image_flip_t::reverse_xy | rye::image_flip_t::transpose;
				  break;
			  default:
				  orientation = rye::image_flip_t::none;
				  break;
		  }
	  });

	size_t entry = 0U;
	for (; entry < x3f.image_entries.size(); ++entry)
		if (x3f.image_entries[entry].versioned.visit(
		      [](const auto &data)
		      {
			      return (data.type == lak::x3f::image_type::RAW ||
			              data.type == lak::x3f::image_type::RAW_Merrill_Quattro) &&
			             (data.format == lak::x3f::image_format::x530 ||
			              data.format == lak::x3f::image_format::SD9_SD10_SD14 ||
			              data.format ==
			                lak::x3f::image_format::DP1_DP1S_DP2_Merrill ||
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

	if (x3f.camf_entries.empty())
		return lak::err_t{lak::fmt<u8"Missing CAMF">()};

	rye::image_data result;

	size_t jpg_entry = 0U;
	for (; jpg_entry < x3f.image_entries.size(); ++jpg_entry)
		if (x3f.image_entries[jpg_entry].versioned.visit(
		      [&](const auto &data)
		      {
			      return data.format == lak::x3f::image_format::JPEG_888_RGB &&
			             (data.columns + 20U) >= out_size.x &&
			             (data.rows + 20U) >= out_size.y;
		      }))
			break;
	if (jpg_entry < x3f.image_entries.size() &&
	    x3f.image_entries[jpg_entry].data.size() > 16U &&
	    lak::compare<byte_t>(
	      lak::span(x3f.image_entries[jpg_entry].data).subspan(6U, 4U),
	      lak::span<const byte_t>("Exif"_span)) == 4U)
	{
		// first pass: get EXIF from the full res thumbnail
		lak::binary_reader jstrm{
		  lak::span(x3f.image_entries[jpg_entry].data).subspan(12U)};
		RES_TRYF_ASSIGN(auto exif =,
		                jstrm.template read<lak::tiff::tiff>().map_err(
		                  [](auto &&err)
		                  {
			                  return err.visit([](auto &&err)
			                                   { return lak::fmt<u8"{}">(err); });
		                  }));
		auto do_tag = [&](const lak::tiff::ifd_tag &tag)
		{
			switch (tag.id)
			{
				case lak::tiff::tag_name::Make:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const char> data)
					  {
						  result.camera.make = lak::to_u8string(
						    lak::astring_view::from_c_str(data.begin(), data.size()));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::Model:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const char> data)
					  {
						  result.camera.model = lak::to_u8string(
						    lak::astring_view::from_c_str(data.begin(), data.size()));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::LensMake:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const char> data)
					  {
						  result.lens.make = lak::to_u8string(
						    lak::astring_view::from_c_str(data.begin(), data.size()));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::LensModel:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const char> data)
					  {
						  result.lens.model = lak::to_u8string(
						    lak::astring_view::from_c_str(data.begin(), data.size()));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::FNumber:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const lak::tiff::urational> data)
					  {
						  if (data.size() > 0U)
							  result.aperture = float(double(data[0].numerator) /
							                          double(data[0].denominator));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::ExposureTime:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const lak::tiff::urational> data)
					  {
						  if (data.size() > 0U)
							  result.shutter = float(double(data[0].numerator) /
							                         double(data[0].denominator));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::FocalLength:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const lak::tiff::urational> data)
					  {
						  if (data.size() > 0U)
							  result.focal_length = float(double(data[0].numerator) /
							                              double(data[0].denominator));
					  },
					  [](...) {},
					});
					break;
				case lak::tiff::tag_name::FocalLengthIn35mmFormat:
					tag.data.visit(lak::overloaded{
					  [&](lak::span<const uint16_t> data)
					  {
						  if (data.size() > 0U) result.focal_length_35mm = float(data[0]);
					  },
					  [](...) {},
					});
					break;
				default:
					break;
			}
		};
		for (const auto &ifd : exif.ifd)
		{
			for (const auto &tag : ifd.tags) do_tag(tag);
			for (const auto &subifd : ifd.subifds)
				for (const auto &tag : subifd.tags) do_tag(tag);
			if (ifd.exif)
				for (const auto &tag : ifd.exif->tags) do_tag(tag);
		}
	}

	lak::vec4s_t image_crop{0U, 0U, out_size.x, out_size.y};

	auto parse_float =
	  [](lak::u16string_view str) -> lak::result<float, lak::u8string>
	{
		constexpr auto float_parser =
		  lak::dsl::parsed_dec_float<float,
		                             lak::dsl::char_literal<U'.'>,
		                             lak::dsl::char_literal<U'e'>>;
		auto u8str = lak::to_u8string(str);
		auto res   = float_parser.parse(u8str);
		if_let_err (auto err, res) return lak::err_t{err.to_string()};
		return lak::ok_t{res.unsafe_unwrap().value};
	};

	if (!x3f.prop_entries.empty())
		for (const auto &e : x3f.prop_entries[0].entries)
		{
			if (e.name == u"CAMMANUF"_view)
			{
				result.camera.make = lak::to_u8string(e.string_data());
			}
			else if (e.name == u"CAMMODEL"_view)
			{
				result.camera.model = lak::to_u8string(e.string_data());
			}
			else if (e.name == u"CAMSERIAL"_view)
			{
				result.camera.serial = lak::to_u8string(e.string_data());
			}
			else if (e.name == u"LENSMODEL"_view)
			{
				result.lens.model = lak::to_u8string(e.string_data());
			}
			else if (e.name == u"APERTURE"_view)
			{
				RES_TRYF_ASSIGN(result.aperture =, parse_float(e.string_data()));
			}
			else if (e.name == u"FLENGTH"_view)
			{
				RES_TRYF_ASSIGN(result.focal_length =, parse_float(e.string_data()));
			}
			else if (e.name == u"FLEQ35MM"_view)
			{
				RES_TRYF_ASSIGN(result.focal_length_35mm =,
				                parse_float(e.string_data()));
			}
			else if (e.name == u"SHUTTER"_view)
			{
				RES_TRYF_ASSIGN(result.shutter =, parse_float(e.string_data()));
			}
			else if (e.name == u"ISO"_view)
			{
				RES_TRYF_ASSIGN(result.iso =, parse_float(e.string_data()));
			}
		}

	if (lak::compare<char8_t>(lak::span(result.camera.make),
	                          lak::span(result.camera.model)) ==
	    result.camera.make.size())
	{
		auto model = lak::u8string_view(result.camera.model)
		               .substr(result.camera.make.size());
		while (!model.empty() && model[0] == u8' ') model = model.substr(1U);
		result.camera.model = model.to_string();
	}

	for (const auto &e : x3f.camf_entries[0].entries)
	{
		if (e.name == "CameraSerialNumber"_view)
		{
			e.data.visit(lak::overloaded{
			  [&](const lak::astring &str)
			  { result.camera.serial = lak::to_u8string(str); },
			  [](...) {},
			});
		}
		else if (e.name == "ActiveImageArea"_view)
		{
			e.data.visit(lak::overloaded{
			  [&](const lak::x3f::camf_matrix &matrix)
			  {
				  matrix.data.visit(lak::overloaded{
				    [&]<typename T>(const lak::x3f::camf_matrix::matrix_type<T> &m)
				    {
					    if (m.data.size() == 4U)
						    image_crop = lak::vec4s_t{
						      static_cast<size_t>(m.data[0]),
						      static_cast<size_t>(m.data[1]),
						      static_cast<size_t>(m.data[2]) -
						        static_cast<size_t>(m.data[0]),
						      static_cast<size_t>(m.data[3]) -
						        static_cast<size_t>(m.data[1]),
						    };
				    },
				    [](const lak::monostate &) {},
				  });
			  },
			  [](...) {},
			});
		}
		else if (e.name == "CaptureISO"_view)
		{
			e.data.visit(lak::overloaded{
			  [&](const lak::x3f::camf_matrix &matrix)
			  {
				  matrix.data.visit(lak::overloaded{
				    [&]<typename T>(const lak::x3f::camf_matrix::matrix_type<T> &m)
				    {
					    if (m.data.size() == 1U)
						    result.iso = static_cast<float>(m.data[0]);
				    },
				    [](const lak::monostate &) {},
				  });
			  },
			  [](...) {},
			});
		}
		else if (e.name == "CaptureAperture"_view)
		{
			e.data.visit(lak::overloaded{
			  [&](const lak::x3f::camf_matrix &matrix)
			  {
				  matrix.data.visit(lak::overloaded{
				    [&]<typename T>(const lak::x3f::camf_matrix::matrix_type<T> &m)
				    {
					    if (m.data.size() == 1U)
						    result.aperture = static_cast<float>(m.data[0]);
				    },
				    [](const lak::monostate &) {},
				  });
			  },
			  [](...) {},
			});
		}
		else if (e.name == "CamToXYZ_Flash"_view || e.name == "CamToXYZ"_view)
		{
			e.data.visit(lak::overloaded{
			  [&](const lak::x3f::camf_matrix &matrix)
			  {
				  matrix.data.visit(lak::overloaded{
				    [&]<typename T>(const lak::x3f::camf_matrix::matrix_type<T> &m)
				    {
					    if (m.data.size() == 9U)
					    {
						    result.cam_to_XYZ = lak::mat3f_t{
						      lak::vec3f_t{
						        static_cast<float>(m.data[0]),
						        static_cast<float>(m.data[1]),
						        static_cast<float>(m.data[2]),
						      },
						      lak::vec3f_t{
						        static_cast<float>(m.data[3]),
						        static_cast<float>(m.data[4]),
						        static_cast<float>(m.data[5]),
						      },
						      lak::vec3f_t{
						        static_cast<float>(m.data[6]),
						        static_cast<float>(m.data[7]),
						        static_cast<float>(m.data[8]),
						      },
						    };
					    }
				    },
				    [](const lak::monostate &) {},
				  });
			  },
			  [](...) {},
			});
		}
	}

	if (result.camera.make == u8"Polaroid"_view &&
	    result.camera.model == u8"x530"_view)
	{
		result.lens.make  = u8"Polaroid"_str;
		result.lens.model = u8"7.3-21.9mm 1:2.6-3.4"_view;
	}

	lak::tasks tasks{lak::tasks::hardware_max()};

	if ((image_crop.z + image_crop.x) > x3f.image_entries[entry].image.size().x)
		return lak::err_t{lak::fmt<u8"Invalid crop (out width {} > raw width {})">(
		  image_crop.z + image_crop.x, x3f.image_entries[entry].image.size().x)};

	if ((image_crop.w + image_crop.y) > x3f.image_entries[entry].image.size().y)
		return lak::err_t{
		  lak::fmt<u8"Invalid crop (out height {} > raw height {})">(
		    image_crop.w + image_crop.y, x3f.image_entries[entry].image.size().y)};

	result.data.resize({image_crop.z, image_crop.w});

	for (size_t y = 0U, sy = image_crop.y; y < result.data.size().y; ++y, ++sy)
		for (size_t x = 0U, sx = image_crop.x; x < result.data.size().x; ++x, ++sx)
			result.data[{x, y}] = lak::vec3f_t(
			  lak::int_to_frac<float>(x3f.image_entries[entry].image[{sx, sy}].x,
			                          linear_max),
			  lak::int_to_frac<float>(x3f.image_entries[entry].image[{sx, sy}].y,
			                          linear_max),
			  lak::int_to_frac<float>(x3f.image_entries[entry].image[{sx, sy}].z,
			                          linear_max));

	if (orientation != rye::image_flip_t::none)
		result.data = rye::transform(tasks, result.data, orientation);

	result.XYZ_to_cam = lak::inverse(result.cam_to_XYZ);
	result.cam_to_sRGB =
	  lak::col::sRGB_primaries.XYZ_to_linear() * result.cam_to_XYZ;

	result.sensor = rye::sensor_format_t::foveon;

	return lak::move_ok(result);
}
