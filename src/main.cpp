#define LAK_BASIC_PROGRAM_IMGUI_WINDOW_IMPL
#define LAK_BASIC_PROGRAM_IMPLOT_IMPL
#define LAK_BASIC_PROGRAM_IMPLOT3D_IMPL
#include <lak/basic_program.hpp>

#include <lak/system/architecture.hpp>

#include "main.hpp"

#include "image_data.hpp"
#include "libraw_format.hpp"
#include "rye.hpp"

#include <lak/format.hpp>
#include <lak/future.hpp>
#include <lak/strconv.hpp>
#include <lak/system/file.hpp>
#include <lak/tasks.hpp>
#include <lak/test.hpp>

#include <lak/file/tiff.hpp>

#include <lak/string_literals/span.hpp>
#include <lak/string_literals/string.hpp>
#include <lak/string_literals/view.hpp>

#include <lak/imgui/texture.hpp>
#include <lak/imgui/widgets.hpp>

#include <stb_image_write.h>

#include <libraw/libraw.h>

#include <filesystem>

#include <inttypes.h>

#include <lak/basic_program.inl>

#include <unordered_map>

bool force_only_error = false;

lak::fs::path binary_path;
lak::optional<lak::future<lak::result<rye::image_data, lak::u8string>>>
  image_load;
lak::optional<lak::future<void>> binary_load, image_process;
lak::optional<rye::image_data> raw_image;
bool binary_update = false, raw_update = false;

int last_white_level = -1;
lak::ImUniqueTexture lrdebayertex, lrprocessedtex, lrsrgbtex, lrwavetex,
  lrwave2tex;

void reset_textures()
{
	lrprocessedtex.reset();
	lrdebayertex.reset();
	lrsrgbtex.reset();
	lrwavetex.reset();
	lrwave2tex.reset();
}

lak::image<lak::vec3f_t> lrdimg, lrpimg, lrsrgbimg, lrwaveimg, lrwave2img;
uint16_t out_colour_temp;

lak::array<lak::vec3f_t> _ir_histo, ir_histo, _white_histo, white_histo,
  _srgb_histo, srgb_histo;

bool use_database_ir_balance = true;
bool use_database_aero_match = true;
bool used_ir_balance_from_db = false;
bool used_aero_match_from_db = false;
struct rye_ir_balance
{
	lak::vec3f_t ir_in{1.f, 1.f, 1.f};
	lak::vec3f_t aero_match{.7f, .35f, 1.4f};
};

std::unordered_map<lak::astring, rye_ir_balance> ir_balance_db = {
  {"Canon EOS 1200D"_str,
   {
     // Not tested with 850nm
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.77f, .63f, 1.f},
   }},
  {"Canon EOS M50"_str,
   {
     .ir_in      = {.930f, .730f, 1.f},
     .aero_match = {.395f, .35f, 1.0f},
   }},
  {"Canon EOS M6 Mark II"_str,
   {
     .ir_in      = {1.07f, .9f, 1.f},
     .aero_match = {.57f, .42f, 1.0f},
   }},
  {"Canon EOS R5"_str,
   {
     .ir_in      = {1.01f, .95f, 1.f},
     .aero_match = {.77f, .35f, .65f},
   }},
  {"Fujifilm X-T30"_str,
   {
     .ir_in      = {1.018f, .978f, 1.f},
     .aero_match = {.46f, .63f, 1.0f},
   }},
  {"Nikon D5200"_str,
   {
     .ir_in      = {1.036f, .690f, 1.f},
     .aero_match = {1.f, .2f, .55f},
   }},
  {"Nikon Z fc"_str,
   {
     .ir_in      = {1.07f, .83f, 1.f},
     .aero_match = {.9f, .44f, 1.f},
   }},
  {"Sigma sd Quattro"_str,
   {
     .ir_in      = {2.f, .373f, .146f},
     .aero_match = {1.f, 1.f, 1.f},
   }},
  {"Sigma sd Quattro H"_str,
   {
     .ir_in      = {1.79f, .335f, .25f},
     .aero_match = {1.f, 1.f, 1.f},
   }},
  {"Sony ILCE-7RM2"_str,
   {
     .ir_in      = {1.030f, 1.073f, 1.f},
     .aero_match = {.85f, .5f, 1.0f},
   }},
};

void load_binary_async(const lak::fs::path &path)
{
	lrdimg.resize({0, 0});
	lrdimg.resize({0, 0});
	lrsrgbimg.resize({0, 0});
	lrwaveimg.resize({0, 0});
	lrwave2img.resize({0, 0});
	image_load = rye::load_image_async(path);
}

void process_image_ir_stage_1(lak::tasks &tasks,
                              lak::image<lak::vec3f_t> &img,
                              lak::vec3f_t ir_in)
{
	if (raw_image->sensor == rye::sensor_format_t::foveon)
	{
		const lak::mat3f_t ir_channel_swap{
		  lak::vec3f_t{0.f, 0.f, 1.f / ir_in.b},
		  lak::vec3f_t{1.f, 0.f, -ir_in.r / ir_in.b},
		  lak::vec3f_t{0.f, 1.f, -ir_in.g / ir_in.b},
		};

		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  img[{x, y}] = ir_channel_swap * img[{x, y}];
				  }
			  });
		}

		tasks.await();
	}
	else
	{
		const float ir_in_red   = ir_in.r / ir_in.b;
		const float ir_in_green = ir_in.g / ir_in.b;

		const lak::mat3f_t ir_channel_swap{
		  lak::vec3f_t{0.f, 0.f, 1.f},
		  lak::vec3f_t{1.f, 0.f, -ir_in.r / ir_in.b},
		  lak::vec3f_t{0.f, 1.f, -ir_in.g / ir_in.b},
		};

		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  img[{x, y}] = ir_channel_swap * img[{x, y}];
				  }
			  });
		}

		tasks.await();
	}
}

void process_image_ir_stage_2(lak::tasks &tasks,
                              lak::image<lak::vec3f_t> &img,
                              lak::vec3f_t aero_match,
                              float colour_temp)
{
	LAK_UNUSED(img);

	auto wb_wv      = rye::relative_blackbody(colour_temp);
	out_colour_temp = uint16_t(std::max<long long>(
	  0, std::min<long long>((1U << 15U) - 1, std::llround(colour_temp))));

	// camera sensitivity compensation
	const lak::vec3f_t aero_match_balance = rye::white_balance(
	  {1.f / aero_match.r, 1.f / aero_match.g, 1.f / aero_match.b});

	// blackbody whitebalance
	const lak::vec3f_t temp_sensitivity{
	  wb_wv(850.0), wb_wv(600.0), wb_wv(525.0)};
	const lak::vec3f_t temp_balance = rye::white_balance(temp_sensitivity);

	// aerochrome sensitivity factor
	const lak::vec3f_t aerochrome_sensitivity{
	  std::exp(0.5f), std::exp(1.5f), std::exp(1.4f)};
	const lak::vec3f_t aero_balance = rye::white_balance(aerochrome_sensitivity);

	const lak::vec3f_t balance =
	  aero_balance * aero_match_balance * temp_balance;

	for (size_t y = 0; y < lrdimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < lrdimg.size().x; ++x)
			  {
				  lak::vec3f_t &irrgb = lrdimg[{x, y}];

				  const float min = rye::vec_min<float>(irrgb);
				  if (min < 0.0f) irrgb -= {min, min, min};

				  irrgb *= balance;
			  }
		  });
	}
	tasks.await();
}

void process_image(int white_level,
                   rye_ir_balance ir_balance,
                   float colour_temp,
                   float exposure,
                   float lightness,
                   float contrast,
                   float saturation,
                   lak::optional<float> desqueeze)
{
	lak::tasks tasks{lak::tasks::hardware_max()};

	bool is_foveon = raw_image->sensor == rye::sensor_format_t::foveon;
	[[maybe_unused]] bool is_xtrans =
	  raw_image->sensor == rye::sensor_format_t::xtrans;

	lrdimg = raw_image->data;

	// convert to sRGB
	if_let_some (float stretch, desqueeze)
		lrpimg.resize(
		  {static_cast<size_t>(static_cast<double>(lrdimg.size().x) * stretch),
		   lrdimg.size().y});
	else
		lrpimg.resize(lrdimg.size());
	for (size_t y = 0; y < lrpimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  auto effect = [&](lak::vec3f_t p) -> lak::vec3f_t
			  {
				  if (is_foveon)
				  {
					  p *= raw_image->whitebalance_coef;

					  p = raw_image->cam_to_sRGB * p;
				  }

				  p =
				    rye::exp_correction(p, exposure, lightness, contrast, saturation);
				  p = rye::to_srgb(p);
				  return p;
			  };
			  if (desqueeze)
			  {
				  auto sampler = rye::desqueeze_sampler(lrdimg, lrpimg.size());
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrpimg.size().x; ++xy.x)
					  lrpimg[xy] = effect(sampler(xy));
			  }
			  else
			  {
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrpimg.size().x; ++xy.x)
					  lrpimg[xy] = effect(lrdimg[xy]);
			  }
		  });
	}
	tasks.await();

	// generate ir balance histogram and waveform
	{
		lak::image<lak::vec3f_t> wavetemp = lrdimg;
		if (is_foveon)
		{
			// on foveon sensors, even though blue is still our main IR-only channel,
			// the IR primarily ends up in the red channel, so we have to be extra
			// careful about exposure compensation.
			for (size_t y = 0; y < wavetemp.size().y; ++y)
			{
				tasks.push(
				  [&, y = y]()
				  {
					  for (size_t x = 0; x < wavetemp.size().x; ++x)
					  {
						  lak::vec3f_t &irgb = wavetemp[{x, y}];
						  irgb.b /= ir_balance.ir_in.b;
						  irgb.g += irgb.b - (ir_balance.ir_in.g * irgb.b);
						  irgb.r += irgb.b - (ir_balance.ir_in.r * irgb.b);
					  }
				  });
			}

			tasks.await();
		}
		else
		{
			// on bayer/x-trans sensors, blue is our primary IR channel
			const float ir_in_red   = ir_balance.ir_in.r / ir_balance.ir_in.b;
			const float ir_in_green = ir_balance.ir_in.g / ir_balance.ir_in.b;
			for (size_t y = 0; y < wavetemp.size().y; ++y)
			{
				tasks.push(
				  [&, y = y]()
				  {
					  for (size_t x = 0; x < wavetemp.size().x; ++x)
					  {
						  lak::vec3f_t &irgb = wavetemp[{x, y}];
						  irgb.g += irgb.b - (ir_in_green * irgb.b);
						  irgb.r += irgb.b - (ir_in_red * irgb.b);
					  }
				  });
			}

			tasks.await();
		}

		lrwaveimg = rye::waveform(wavetemp);
		_ir_histo = rye::histogram(wavetemp);
	}

	process_image_ir_stage_1(tasks, lrdimg, ir_balance.ir_in);

	process_image_ir_stage_2(tasks, lrdimg, ir_balance.aero_match, colour_temp);

	// generate white balance histogram and waveform
	lrwave2img   = rye::waveform(lrdimg);
	_white_histo = rye::histogram(lrdimg);

	// convert to sRGB
	if_let_some (float stretch, desqueeze)
		lrsrgbimg.resize(
		  {static_cast<size_t>(static_cast<double>(lrdimg.size().x) * stretch),
		   lrdimg.size().y});
	else
		lrsrgbimg.resize(lrdimg.size());
	for (size_t y = 0; y < lrsrgbimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  if (desqueeze)
			  {
				  auto sampler = rye::desqueeze_sampler(lrdimg, lrsrgbimg.size());
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrsrgbimg.size().x; ++xy.x)
				  {
					  lrsrgbimg[xy] = sampler(xy);
					  lrsrgbimg[xy] = rye::exp_correction(
					    lrsrgbimg[xy], exposure, lightness, contrast, saturation);
					  lrsrgbimg[xy] = rye::to_srgb(lrsrgbimg[xy]);
				  }
			  }
			  else
			  {
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrsrgbimg.size().x; ++xy.x)
				  {
					  lrsrgbimg[xy] = lrdimg[xy];
					  lrsrgbimg[xy] = rye::exp_correction(
					    lrsrgbimg[xy], exposure, lightness, contrast, saturation);
					  lrsrgbimg[xy] = rye::to_srgb(lrsrgbimg[xy]);
				  }
			  }
		  });
	}
	tasks.await();

	// generate final histogram
	_srgb_histo = rye::histogram(lrsrgbimg);
}

void process_image_async(int white_level,
                         rye_ir_balance balance,
                         float colour_temp,
                         float exposure,
                         float lightness,
                         float contrast,
                         float saturation,
                         lak::optional<float> desqueeze)
{
	image_process = lak::async(process_image,
	                           white_level,
	                           balance,
	                           colour_temp,
	                           exposure,
	                           lightness,
	                           contrast,
	                           saturation,
	                           desqueeze);
}

struct rye_window : virtual public basic_window_api
{
	rye_window() : basic_window_api() {}

	const lak::cobalt::graphics_context *gc;

	virtual ~rye_window() { reset_textures(); }

	virtual void init() override final
	{
		lak::debugger.crash_path = std::filesystem::current_path() /
		                           "ATTACH-TO-ISSUE-ON-RYE-GITHUB-REPO.txt";

		lak::debugger.live_output_enabled = true;

		ASSERT_EQUAL(window().graphics(), lak::graphics_mode::Cobalt);
		gc = &lak::cobalt_graphics_context(window().handle()).UNWRAP();
		ASSERT(!!gc);
		ASSERT(!!gc->renderer);

		auto graphics_string = lak::fmt<"{} {}">(gc->api_family, gc->api_version);
		DEBUG("Graphics: ", graphics_string);
		if (!lak::debugger.live_output_enabled || lak::debugger.live_errors_only)
			std::cout << "Graphics: " << graphics_string << "\n";

		window().set_title(L"" APP_NAME);
	}

	virtual void handle_event(lak::event &event) override final
	{
		switch (event.type)
		{
			case lak::event_type::close_window:
				destroy();
				break;

			case lak::event_type::dropfile:
				load_binary_async(lak::fs::path(event.dropfile().path));
				break;

			default:
				break;
		}
	}

	void open_file(const lak::fs::path &path) { load_binary_async(path); }

	void save_png_file(const lak::fs::path &path,
	                   const lak::image<lak::vec3f_t> &img)
	{
		lak::image3_t processedimg;
		processedimg.resize(img.size());

		{
			lak::tasks tasks{lak::tasks::hardware_max()};
			for (size_t y = 0; y < img.size().y; ++y)
			{
				tasks.push(
				  [&, y = y]()
				  {
					  for (size_t x = 0; x < img.size().x; ++x)
					  {
						  auto clamp = [](float f) -> uint8_t
						  {
							  if (f >= 1.f)
								  return 255;
							  else if (f <= 0.f)
								  return 0;
							  else
								  return static_cast<uint8_t>(f * 255.f);
						  };
						  processedimg[{x, y}].r = clamp(img[{x, y}].r);
						  processedimg[{x, y}].g = clamp(img[{x, y}].g);
						  processedimg[{x, y}].b = clamp(img[{x, y}].b);
					  }
				  });
			}
		}

		stbi_write_png(
		  (const char *)path.u8string().c_str(),
		  int(processedimg.size().x),
		  int(processedimg.size().y),
		  3,
		  processedimg.data(),
		  int(processedimg.contig_size_bytes() / processedimg.size().y));
	}

	void save_dng_file(const lak::fs::path &path)
	{
		lak::binary_array_writer strm;

		lak::tiff::tiff tiff;

		tiff.ifd.clear();

		// tags must be in ascending order by id

		// use enhanced image data?

		tiff.ifh.version = 42;
		auto &ifd0       = tiff.ifd.emplace_back();

		lak::image<lak::vec3u16_t> img16;
		img16.resize(lrdimg.size());

		for (lak::vec2s_t xy = {0, 0}; xy.y < img16.size().y; ++xy.y)
		{
			for (xy.x = 0; xy.x < img16.size().x; ++xy.x)
			{
				img16[xy].r = lak::frac_to_int<uint16_t>(lrdimg[xy].r);
				img16[xy].g = lak::frac_to_int<uint16_t>(lrdimg[xy].g);
				img16[xy].b = lak::frac_to_int<uint16_t>(lrdimg[xy].b);
			}
		}

		static_assert(
		  lak::to_bytes_traits<lak::vec3u16_t, lak::endian::native>::const_size);
		size_t row_size_bytes =
		  img16.size().x *
		  lak::to_bytes_traits<lak::vec3u16_t, lak::endian::native>::size;
		[[maybe_unused]] size_t img_size_bytes = img16.size().y * row_size_bytes;

		// strips may not exceed 64KB decompressed
		ifd0.rows = static_cast<uint32_t>(64'000U / row_size_bytes);
		ASSERT_GREATER(ifd0.rows, 0U);
		size_t strip_count = lak::ceil_div<size_t>(img16.size().y, ifd0.rows);
		ASSERT_GREATER(strip_count, 0U);

		for (size_t s = 0; s < strip_count; ++s)
		{
			auto &ifd0_strip = ifd0.strips.emplace_back();

			size_t row_start = s * ifd0.rows;
			size_t row_end = std::min<size_t>(row_start + ifd0.rows, img16.size().y);

			size_t begin = row_start * lrdimg.size().x;
			size_t count = (row_end - row_start) * lrdimg.size().x;

			ifd0_strip.data.resize((row_end - row_start) * row_size_bytes);
			lak::binary_span_writer{lak::span(ifd0_strip.data)}
			  .write<lak::endian::native>(
			    lak::span<const lak::vec3u16_t>(img16.data() + begin, count))
			  .UNWRAP();
		}

		ifd0.push_NewSubfileType(lak::fixed_array(uint32_t(0U)));
		ifd0.push_ImageWidth(
		  lak::fixed_array(static_cast<uint32_t>(img16.size().x)));
		ifd0.push_ImageLength(
		  lak::fixed_array(static_cast<uint32_t>(img16.size().y)));
		ifd0.push_BitsPerSample(
		  lak::fixed_array(uint16_t(16U), uint16_t(16U), uint16_t(16U)));
		ifd0.push_Compression(lak::fixed_array(uint16_t(1U)));
		// LinearRaw
		ifd0.push_PhotometricInterpretation(lak::fixed_array(uint16_t(34892U)));
		ifd0.push_Make(
		  lak::astring_view((const char *)raw_image->camera.make.c_str()));
		ifd0.push_Model(
		  lak::astring_view((const char *)raw_image->camera.model.c_str()));
		ifd0.push_Orientation(lak::fixed_array((uint16_t(1U))));
		ifd0.push_SamplesPerPixel(lak::fixed_array(uint16_t(3U)));

		ifd0.push_XResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_YResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_ResolutionUnit(lak::fixed_array(uint16_t(2U)));
		ifd0.push_Software(APP_NAME ""_view);
		ifd0.push_SampleFormat({1U});

		if (raw_image->shutter != 0.f)
			ifd0.push_ExposureTime(lak::fixed_array(lak::tiff::urational{
			  1000U, uint32_t((1.f / raw_image->shutter) * 1000)}));
		ifd0.push_FNumber(lak::fixed_array(
		  lak::tiff::urational{uint32_t(raw_image->aperture * 100), 100U}));
		ifd0.push_ISOSpeedRatings(lak::fixed_array(uint16_t(raw_image->iso)));
		ifd0.push_FocalLength(lak::fixed_array(
		  lak::tiff::urational{uint32_t(raw_image->focal_length * 100), 100U}));

		ifd0.push_DNGVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_DNGBackwardVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_UniqueCameraModel(lak::string_view(
		  lak::fmt<"{} {}">(raw_image->camera.make, raw_image->camera.model)));
		ifd0.push_CameraSerialNumber(
		  lak::astring_view((const char *)raw_image->camera.serial.c_str()));

		auto &exif = ifd0.push_exif();

		exif.push_LensMake(
		  lak::astring_view((const char *)raw_image->lens.make.c_str()));
		exif.push_LensModel(
		  lak::astring_view((const char *)raw_image->lens.model.c_str()));
		exif.push_LensSerialNumber(
		  lak::astring_view((const char *)raw_image->lens.serial.c_str()));

		strm.write<lak::endian::native>(tiff).UNWRAP();

		lak::save_file(path, strm.data);
	}

	const lak::fs::path &file_path() { return binary_path; }

	bool update() { return binary_update || raw_update; }

	lak::path_getter open_pgetter, save_png_pgetter, save_dng_pgetter;
	bool save_srgb_png = false;

	void file_menu()
	{
		if (auto res = open_pgetter(); res) open_file(*res);
		if (auto res = save_png_pgetter(); res)
			save_png_file(*res, save_srgb_png ? lrsrgbimg : lrdimg);
		if (auto res = save_dng_pgetter(); res) save_dng_file(*res);

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open...", nullptr, false))
				open_pgetter.open_file(
				  file_path(),
				  "Raw Image Files{.ARW,.RAF,.NEF,.CR3,.CR2,.DNG,.X3F},.*");

			if (ImGui::MenuItem(
			      "Save DNG...", nullptr, false, lrdimg.contig_size() != 0U))
			{
				save_dng_pgetter.save_file(
				  file_path().parent_path() /
				    (file_path().stem().u8string() + u8".DNG"),
				  "Image Files{.DNG}");
			}

			if (ImGui::MenuItem("Save PNG (linear)...",
			                    nullptr,
			                    false,
			                    lrdimg.contig_size() != 0U))
			{
				save_srgb_png = false;
				save_png_pgetter.save_file(
				  file_path().parent_path() /
				    (file_path().stem().u8string() + u8".PNG"),
				  "Image Files{.PNG}");
			}

			if (ImGui::MenuItem("Save PNG (sRGB)...",
			                    nullptr,
			                    false,
			                    lrsrgbimg.contig_size() != 0U))
			{
				save_srgb_png = true;
				save_png_pgetter.save_file(
				  file_path().parent_path() /
				    (file_path().stem().u8string() + u8".PNG"),
				  "Image Files{.PNG}");
			}

			// if (ImGui::MenuItem(
			//       "Save DNG...", nullptr, false, lrdimg.contig_size() != 0U))
			// 	save_dng_pgetter.save_file(
			// 	  file_path().parent_path() /
			// 	    (file_path().stem().u8string() + u8".DNG"),
			// 	  "Image Files{.DNG}");

			ImGui::EndMenu();
		}
	}

	void credits() { ::credits(); }

	void about_menu(float frame_time)
	{
		if (ImGui::BeginMenu("About"))
		{
			ImGui::Text(APP_NAME " by LAK132");
			ImGui::Text("Frame rate %f", std::round(1.0f / frame_time));
			ImGui::Text("Perf Freq  0x%016" PRIX64, lak::performance_frequency());
			ImGui::Text("Perf Count 0x%016" PRIX64, lak::performance_counter());
			credits();
			ImGui::EndMenu();
		}
	}

	void load_db_data()
	{
		used_ir_balance_from_db = false;
		used_aero_match_from_db = false;
		if (use_database_ir_balance || use_database_aero_match)
		{
			if (auto it = ir_balance_db.find(lak::fmt<"{} {}">(
			      raw_image->camera.make, raw_image->camera.model));
			    it != ir_balance_db.end())
			{
				if (use_database_ir_balance)
				{
					ir_balance.ir_in        = it->second.ir_in;
					used_ir_balance_from_db = true;
				}
				if (use_database_aero_match)
				{
					ir_balance.aero_match   = it->second.aero_match;
					used_aero_match_from_db = true;
				}
			}
		}
	}

	void menu_bar(float frame_time)
	{
		file_menu();
		about_menu(frame_time);
		if (ImGui::Checkbox("Use sensor database IR white point",
		                    &use_database_ir_balance))
			load_db_data();
		if (ImGui::Checkbox("Use sensor database RGB white point",
		                    &use_database_aero_match))
			load_db_data();
	}

	int lraw_white_level = UINT16_MAX;
	rye_ir_balance ir_balance;
	float time_acc      = 0.0f;
	float lraw_contrast = 1.f;
	float colour_temp   = 5500;
	float exposure      = 0.f;
	float lightness     = 0.f;
	float contrast      = 0.f;
	float saturation    = 0.f;
	bool anamorphic     = false;
	float desqueeze     = 1.f;

	float left_size  = -1.f;
	float right_size = -1.f;

	float lrawtex_size     = 0.5f;
	float lrawptex_size    = 0.5f;
	float lrawwtex_size    = 1.f;
	float lraww2tex_size   = 1.f;
	float lrawdtex_size    = 1.f;
	float lrawsrgbtex_size = 1.f;

	void main_region(float frame_time)
	{
		if (image_load)
		{
			ImGui::BeginChild(
			  "Mid", {-1, -1}, true, ImGuiWindowFlags_NoSavedSettings);
			time_acc += frame_time;
			if (time_acc > 3.0f) time_acc -= std::trunc(time_acc);
			if (time_acc > 2.0f)
				ImGui::Text("Loading...");
			else if (time_acc > 1.0f)
				ImGui::Text("Loading..");
			else
				ImGui::Text("Loading.");

			if (image_load->has_value())
			{
				auto &res = image_load->wait();
				DEFER(image_load.reset());
				if_let_ok (auto &img_data,
				           res.if_err([](const lak::u8string &str) { ERROR(str); }))
				{
					raw_image.emplace(lak::move(img_data));
					binary_update = true;
					raw_update    = true;
					time_acc      = 0.f;

					reset_textures();

					ir_histo.clear();
					white_histo.clear();
					srgb_histo.clear();

					load_db_data();
				}
			}
			ImGui::EndChild();
		}
		else if (!raw_image.has_value())
		{
			ImGui::BeginChild(
			  "Mid", {-1, -1}, true, ImGuiWindowFlags_NoSavedSettings);
			ImGui::Text("No file");
			ImGui::EndChild();
		}
		else
		{
			if (image_process && image_process->has_value())
			{
				image_process.reset();
				ir_histo    = lak::move(_ir_histo);
				white_histo = lak::move(_white_histo);
				srgb_histo  = lak::move(_srgb_histo);
				lrprocessedtex.emplace(lrpimg);
				lrdebayertex.emplace(lrdimg);
				lrsrgbtex.emplace(lrsrgbimg);
				lrwavetex.emplace(lrwaveimg);
				lrwave2tex.emplace(lrwave2img);
			}

			if (raw_update && !image_process)
			{
				raw_update = false;
				desqueeze  = std::max(.5f, std::min(10.f, desqueeze));
				process_image_async(lraw_white_level,
				                    ir_balance,
				                    colour_temp,
				                    exposure,
				                    lightness,
				                    contrast,
				                    saturation,
				                    anamorphic ? lak::make_optional(desqueeze)
				                               : lak::nullopt);
			}

			{
				const auto content_size{ImGui::GetContentRegionAvail()};

				if (left_size <= 0.f || right_size <= 0.f)
				{
					left_size  = std::min<float>(content_size.x / 2, 500.f);
					right_size = content_size.x - left_size;
				}

				auto draw_histo = [](const lak::astring &label,
				                     lak::span<lak::vec3f_t> data,
				                     ImVec2 size = ImVec2(0, 0))
				{
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
					                      ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
					ImGui::PlotHistogram(
					  ("R" + label).c_str(),
					  [](void *d, int idx) -> float
					  { return reinterpret_cast<lak::vec3f_t *>(d)[idx].r; },
					  (void *)data.data(),
					  static_cast<int>(data.size()),
					  0,
					  nullptr,
					  FLT_MAX,
					  FLT_MAX,
					  size);
					ImGui::PopStyleColor();
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
					                      ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
					ImGui::PlotHistogram(
					  ("G" + label).c_str(),
					  [](void *d, int idx) -> float
					  { return reinterpret_cast<lak::vec3f_t *>(d)[idx].g; },
					  (void *)data.data(),
					  static_cast<int>(data.size()),
					  0,
					  nullptr,
					  FLT_MAX,
					  FLT_MAX,
					  size);
					ImGui::PopStyleColor();
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
					                      ImVec4(0.3f, 0.3f, 1.0f, 1.0f));
					ImGui::PlotHistogram(
					  ("B" + label).c_str(),
					  [](void *d, int idx) -> float
					  { return reinterpret_cast<lak::vec3f_t *>(d)[idx].b; },
					  (void *)data.data(),
					  static_cast<int>(data.size()),
					  0,
					  nullptr,
					  FLT_MAX,
					  FLT_MAX,
					  size);
					ImGui::PopStyleColor();
				};

				lak::VertSplitter(left_size, right_size, content_size.x);

				ImGui::BeginChild("ImgLeft",
				                  {left_size, -1},
				                  true,
				                  ImGuiWindowFlags_NoSavedSettings |
				                    ImGuiWindowFlags_AlwaysVerticalScrollbar);

				const auto left_content_size{ImGui::GetContentRegionAvail()};
				lak::Text<u8"{} {} + {} {}">(raw_image->camera.make,
				                             raw_image->camera.model,
				                             raw_image->lens.make,
				                             raw_image->lens.model);
				ImGui::Text("ISO %.0f 1/%.0fs f/%.0f %.0fmm",
				            raw_image->iso,
				            1.0f / raw_image->shutter,
				            raw_image->aperture,
				            raw_image->focal_length);

				ImGui::Separator();

				ImGui::Text("Raw IR white point");

				if (!use_database_ir_balance || !used_ir_balance_from_db)
				{
					ImGui::DragFloat(
					  "R##IR in", &ir_balance.ir_in.r, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

					ImGui::DragFloat(
					  "G##IR in", &ir_balance.ir_in.g, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

					ImGui::DragFloat(
					  "B##IR in", &ir_balance.ir_in.b, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;
				}
				else
				{
					ImGui::Text(
					  "IR in R: %.3f/%.3f", ir_balance.ir_in.r, ir_balance.ir_in.b);
					ImGui::Text(
					  "IR in G: %.3f/%.3f", ir_balance.ir_in.g, ir_balance.ir_in.b);
				}

				draw_histo("##IR HISTO", ir_histo, ImVec2(0, 60));

				ImGui::Separator();

				ImGui::Text("Raw RGB white point");

				if (!use_database_aero_match || !used_aero_match_from_db)
				{
					ImGui::DragFloat(
					  "R##W", &ir_balance.aero_match.r, 0.001f, 0.001f, 2.0f, "IR/%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

					ImGui::DragFloat(
					  "G##W", &ir_balance.aero_match.g, 0.001f, 0.001f, 2.0f, "R/%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

					ImGui::DragFloat(
					  "B##W", &ir_balance.aero_match.b, 0.001f, 0.001f, 2.0f, "G/%.3f");
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;
				}
				else
				{
					ImGui::Text("R: IR/%.3f\nG: R/%.3f\nB: G/%.3f",
					            ir_balance.aero_match.r,
					            ir_balance.aero_match.g,
					            ir_balance.aero_match.b);
				}

				draw_histo("##WHITE HISTO", white_histo, ImVec2(0, 60));

				ImGui::Separator();

				ImGui::Text("Photo Settings");

				ImGui::DragFloat(
				  "Temperature", &colour_temp, 10.f, 2000.f, 10000.f, "%.0fK");
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::DragFloat(
				  "##DesqueezeInput", &desqueeze, 0.1f, 1.f, 3.f, "%.1fx");
				if (anamorphic && ImGui::IsItemDeactivatedAfterEdit())
					raw_update = true;
				ImGui::SameLine();
				if (ImGui::Checkbox("Desqueeze", &anamorphic)) raw_update = true;

				ImGui::Separator();

				ImGui::Text("Look Settings (Beta)");

				ImGui::DragFloat("Exposure", &exposure, 0.1f, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::DragFloat("Contrast", &contrast, 0.1f, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::DragFloat("Lightness", &lightness, 0.1f, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::DragFloat("Saturation", &saturation, 0.1f, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				draw_histo("##sRGB HISTO", srgb_histo, ImVec2(0, 60));

				if (image_process) ImGui::Text("Processing...");

				ImGui::EndChild();

				ImGui::SameLine();

				ImGui::BeginChild("ImgRight",
				                  {right_size, -1},
				                  true,
				                  ImGuiWindowFlags_NoSavedSettings);
				LAK_TREE_NODE("RAW") { rye::image_view(lrawtex, &lrawtex_size); }
				LAK_TREE_NODE("PROC RAW")
				{
					rye::image_view(lrprocessedtex, &lrawptex_size);
				}
				LAK_TREE_NODE("IR BALANCE WAVEFORM")
				{
					rye::image_view(lrwavetex, &lrawwtex_size);
				}
				LAK_TREE_NODE("WHITE BALANCE WAVEFORM")
				{
					rye::image_view(lrwave2tex, &lraww2tex_size);
				}
				LAK_TREE_NODE("DEBAYER")
				{
					rye::image_view(lrdebayertex, &lrawdtex_size);
				}
				// LAK_TREE_NODE("sRGB")
				{
					rye::image_view(lrsrgbtex, &lrawsrgbtex_size);
				}
				ImGui::EndChild();
			}
		}
	}

	virtual void loop(uint64_t counter_delta) override final
	{
		const float frame_time =
		  (float)counter_delta / lak::performance_frequency();

		if (ImGui::BeginMenuBar())
		{
			menu_bar(frame_time);
			ImGui::EndMenuBar();
		}

		main_region(frame_time);
		if (binary_update)
		{
			window().set_title(L"" APP_NAME " (" + binary_path.generic_wstring() +
			                   L")");
			binary_update = false;
		}
	}
};

lak::error_code<int> basic_program_preinit(lak::span<char *> args)
{
	if (!args.empty()) args = args.subspan(1U);

	if (args.size() == 1U && args[0] == lak::astring("--version"))
	{
		std::cout << APP_NAME << "\n";
		return lak::err_t{EXIT_SUCCESS};
	}

	lak::debugger.std_out(u8"", u8"" APP_NAME "\n");

	for (size_t arg = 0U; arg < args.size(); ++arg)
	{
		if (args[arg] == lak::astring("-h") || args[arg] == lak::astring("--help"))
		{
			std::cout << "rye.exe "
			             "[--help] "
			             "[--onlyerr] "
			             "[<filepath>]\n";

			return lak::err_t{EXIT_SUCCESS};
		}
		else if (args[arg] == lak::astring("--onlyerr"))
		{
			force_only_error = true;
		}
		else
		{
			if (lak::path_exists(args[arg]).UNWRAP())
			{
				load_binary_async(lak::fs::path(args[arg]));
			}
			else
				FATAL("file ", args[arg], " does not exists");
		}
	}

	return lak::ok_t{};
}

lak::weak_ptr<basic_window_instance<rye_window>> rye_wnd;

lak::error_code<int> basic_program_init()
{
	basic_window_target_framerate = 30;

	basic_window_cobalt_settings.depth_mode = cobalt::graphics::IFrameBuffer::
	  WindowDepthStencilMode::DepthUNorm24StencilUInt8;
	basic_window_cobalt_settings.colour_mode =
	  cobalt::graphics::IFrameBuffer::WindowColorSpaceMode::Default;

	rye_wnd = basic_create_window<rye_window>(
	            basic_window_cobalt_settings,
	            lak::cobalt_renderer_settings::feature_set_t{
	              cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
	            .UNWRAP();

	{
		auto window = rye_wnd.get();

		ASSERT(!!window);

		window->imgui_window_flags =
		  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
		  ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings |
		  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove;

		window->clear_colour = {0.0f, 0.0f, 0.0f, 1.0f};
	}

	return lak::ok_t{};
}

void basic_program_handle_event(lak::event &event)
{
	switch (event.type)
	{
		case lak::event_type::quit_program:
			rye_wnd.reset();
			break;

		default:
			break;
	}
}

bool basic_program_loop(uint64_t) { return (bool)rye_wnd.get(); }

int basic_program_quit()
{
	rye_wnd.reset();
	return EXIT_SUCCESS;
}
