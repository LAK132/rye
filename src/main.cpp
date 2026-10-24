#define LAK_BASIC_PROGRAM_IMGUI_WINDOW_IMPL
#define LAK_BASIC_PROGRAM_IMPLOT_IMPL
#define LAK_BASIC_PROGRAM_IMPLOT3D_IMPL
#include <lak/basic_program.hpp>

#include <lak/system/architecture.hpp>
#include <lak/system/cobalt/math.hpp>
#include <lak/system/cobalt/program.hpp>
#include <lak/system/cobalt/result.hpp>
#include <lak/system/cobalt/state.hpp>

#include "rye/image_data.hpp"
#include "rye/rye.hpp"

#include "main.hpp"

#include "image_proc.hpp"
#include "libraw_format.hpp"

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

#include <lak/col/cie.hpp>
#include <lak/col/oprgb.hpp>
#include <lak/col/prophoto.hpp>
#include <lak/col/srgb.hpp>

#include <lak/imgui/cie.hpp>
#include <lak/imgui/texture.hpp>
#include <lak/imgui/widgets.hpp>

#include <stb_image_write.h>

#include <libraw/libraw.h>

#include <filesystem>

#include <inttypes.h>

#include <lak/basic_program.inl>

#include <unordered_map>

bool force_only_error = false;

lak::fs::path load_path;
lak::fs::path binary_path;
lak::optional<lak::future<lak::result<rye::image_data, lak::u8string>>>
  image_load;
lak::optional<lak::future<void>> binary_load, image_process;
lak::optional<rye::image_data> raw_image;
bool binary_update = false, raw_update = false;

int last_white_level = -1;
lak::ImUniqueTexture lrawtex, lrdebayertex, lrprocessedtex, lrsrgbtex,
  lrwavetex, lrwave2tex, finaltex;

void reset_textures()
{
	lrawtex.reset();
	lrprocessedtex.reset();
	lrdebayertex.reset();
	lrsrgbtex.reset();
	lrwavetex.reset();
	lrwave2tex.reset();
	finaltex.reset();
}

lak::image<lak::vec3f_t> lrdimg, lrpimg, lrsrgbimg, lrwaveimg, lrwave2img,
  lrdupimg;
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
     //  .ir_in      = {.930f, .730f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.395f, .35f, 1.0f},
   }},
  {"Canon EOS M6 Mark II"_str,
   {
     //  .ir_in      = {1.07f, .9f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.57f, .42f, 1.0f},
   }},
  {"Canon EOS R5"_str,
   {
     //  .ir_in      = {1.01f, .95f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.77f, .35f, .65f},
   }},
  {"Fujifilm X-T30"_str,
   {
     //  .ir_in      = {1.018f, .978f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.46f, .63f, 1.0f},
   }},
  {"Nikon D5200"_str,
   {
     //  .ir_in      = {1.036f, .690f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {1.f, .2f, .55f},
   }},
  {"Nikon Z fc"_str,
   {
     //  .ir_in      = {1.07f, .83f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.9f, .44f, 1.f},
   }},
  {"Sigma sd Quattro"_str,
   {
     //  .ir_in      = {2.f, .373f, .146f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {1.f, 1.f, 1.f},
   }},
  {"Sigma sd Quattro H"_str,
   {
     //  .ir_in      = {1.79f, .335f, .25f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {1.f, 1.f, 1.f},
   }},
  {"Sony ILCE-7RM2"_str,
   {
     //  .ir_in      = {1.030f, 1.073f, 1.f},
     .ir_in      = {1.f, 1.f, 1.f},
     .aero_match = {.85f, .5f, 1.0f},
   }},
};

void load_binary_async(const lak::fs::path &path)
{
	lrdimg.resize({0, 0});
	lrsrgbimg.resize({0, 0});
	lrwaveimg.resize({0, 0});
	lrwave2img.resize({0, 0});
	lrdupimg.resize({0, 0});
	load_path  = path;
	image_load = rye::load_image_async(path);
}

#if 0
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
#endif

struct rye_window : virtual public basic_window_api
{
	rye_window() : basic_window_api() {}

	const lak::cobalt::graphics_context *gc;
	bool run_compute = false;

	float image_view_scale = 1.f;
	ImVec2 image_view_pos  = ImVec2(0, 0);

	virtual ~rye_window()
	{
		image_viewport_state.reset();
		reset_textures();
	}

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

	bool can_save_gpu_image() const
	{
		return !image_load && raw_image && finaltex &&
		       image_viewport_state->image_buffer &&
		       image_viewport_state->image_capture &&
		       image_viewport_state->pass_node;
	}

	lak::result<lak::image<lak::vec4f_t>, lak::u8string> read_gpu_image()
	{
		if (!can_save_gpu_image()) return lak::err_t{u8"No image to save"_str};

		const auto size = lak::TextureSize(finaltex);
		if (size.x == 0U || size.y == 0U)
			return lak::err_t{u8"Cannot save an empty image"_str};

		auto *rd = gc->renderer.get();
		rd->WaitForOutputCaptureComplete();
		DEFER(rd->WaitForDeferredDeletionComplete());

		auto &state   = *image_viewport_state;
		auto &capture = state.image_capture;

		capture->ClearCapturedOutput();
		DEFER(capture->ClearCapturedOutput());

		state.image_buffer->AddOutputCaptureTarget(capture.get());
		DEFER(state.image_buffer->RemoveOutputCaptureTarget(capture.get()));

		auto *compute_pass = state.pass_node.get();
		rd->SetRenderPasses(&compute_pass, 1);
		DEFER(rd->RemoveAllRenderPasses());
		rd->StartNewFrame();
		rd->WaitForOutputCaptureComplete();

		if (!capture->HasCapturedOutput() ||
		    capture->GetEntryCount() != size.x * size.y)
			return lak::err_t{u8"Failed to capture the complete GPU image"_str};

		lak::image<lak::vec4f_t> img;
		img.resize(size);
		if (!capture->ReadBufferData(
		      img.data(),
		      img.contig_size_bytes(),
		      cobalt::graphics::ITexelArray::SourceImageFormat::RGBA,
		      cobalt::graphics::ITexelArray::SourceDataFormat::Float32))
			return lak::err_t{u8"Failed to read the GPU image"_str};

		return lak::move_ok(img);
	}

	void save_png_file(const lak::fs::path &path)
	{
		auto image = read_gpu_image();
		if_let_err (const auto &error, image)
		{
			ERROR(error);
			return;
		}
		const auto &img = image.UNWRAP();

		lak::image3_t processedimg;
		processedimg.resize(img.size());

		auto to_srgb = [scene_to_XYZ = scene_primaries.linear_to_XYZ(),
		                adaptation   = lak::col::cie::bradford_adaptation_matrix(
                      scene_primaries.w, lak::col::sRGB_primaries.w)](
		                 const lak::vec3f_t &p) -> lak::vec3f_t
		{
			return lak::col::to_sRGB(
			         lak::col::cie::XYZ::from_vec(adaptation * (scene_to_XYZ * p)))
			  .to_vec();
		};
		{
			lak::tasks tasks{lak::tasks::hardware_max()};
			for (size_t y = 0; y < img.size().y; ++y)
			{
				tasks.push(
				  [&, y = y]()
				  {
					  for (lak::vec2s_t xy{0U, y}; xy.x < img.size().x; ++xy.x)
					  {
						  auto srgb          = to_srgb(img[xy].xyz());
						  processedimg[xy].r = lak::frac_to_int<uint8_t>(srgb.r);
						  processedimg[xy].g = lak::frac_to_int<uint8_t>(srgb.g);
						  processedimg[xy].b = lak::frac_to_int<uint8_t>(srgb.b);
					  }
				  });
			}
		}

		if (!stbi_write_png(
		      (const char *)path.u8string().c_str(),
		      int(processedimg.size().x),
		      int(processedimg.size().y),
		      3,
		      processedimg.data(),
		      int(processedimg.contig_size_bytes() / processedimg.size().y)))
			ERROR("Failed to save PNG");
	}

	void save_dng_file(const lak::fs::path &path)
	{
		auto image = read_gpu_image();
		if_let_err (const auto &error, image)
		{
			ERROR(error);
			return;
		}
		const auto &img = image.unsafe_unwrap();

		lak::binary_array_writer strm;

		lak::tiff::tiff tiff;

		tiff.ifd.clear();

		// tags must be in ascending order by id

		// use enhanced image data?

		tiff.ifh.version = 42;
		auto &ifd0       = tiff.ifd.emplace_back();

		lak::image<lak::vec3u16_t> img16;
		img16.resize(img.size());

		{
			lak::tasks tasks{lak::tasks::hardware_max()};
			for (size_t y = 0U; y < img16.size().y; ++y)
			{
				tasks.push(
				  [&, y = y]()
				  {
					  for (lak::vec2s_t xy{0U, y}; xy.x < img16.size().x; ++xy.x)
					  {
						  img16[xy].r = lak::frac_to_int<uint16_t>(img[xy].r);
						  img16[xy].g = lak::frac_to_int<uint16_t>(img[xy].g);
						  img16[xy].b = lak::frac_to_int<uint16_t>(img[xy].b);
					  }
				  });
			}
		}

		static_assert(
		  lak::to_bytes_traits<lak::vec3u16_t, lak::endian::native>::const_size);
		size_t row_size_bytes =
		  img16.size().x *
		  lak::to_bytes_traits<lak::vec3u16_t, lak::endian::native>::size;
		[[maybe_unused]] size_t img_size_bytes = img16.size().y * row_size_bytes;

		// strips may not exceed 64KB decompressed
		ifd0.rows =
		  static_cast<uint32_t>(std::max<size_t>(1U, 64'000U / row_size_bytes));
		ASSERT_GREATER(ifd0.rows, 0U);
		size_t strip_count = lak::ceil_div<size_t>(img16.size().y, ifd0.rows);
		ASSERT_GREATER(strip_count, 0U);

		for (size_t s = 0; s < strip_count; ++s)
		{
			auto &ifd0_strip = ifd0.strips.emplace_back();

			size_t row_start = s * ifd0.rows;
			size_t row_end = std::min<size_t>(row_start + ifd0.rows, img16.size().y);

			size_t begin = row_start * img16.size().x;
			size_t count = (row_end - row_start) * img16.size().x;

			ifd0_strip.data.resize((row_end - row_start) * row_size_bytes);
			lak::binary_span_writer{lak::span(ifd0_strip.data)}
			  .write<lak::endian::native>(
			    lak::span<const lak::vec3u16_t>(img16.data() + begin, count))
			  .UNWRAP();
		}

		auto float_to_rational = [](float value,
		                            uint32_t decimal_places) -> lak::tiff::rational
		{
			int32_t divisor = 1;
			while (decimal_places-- > 0U) divisor *= 10U;
			value *= divisor;
			return lak::tiff::rational{int32_t(value), divisor};
		};

		auto mat_to_rational =
		  [&](lak::mat3f_t mat,
		      uint32_t decimal_places) -> lak::array<lak::tiff::rational, 9U>
		{
			return lak::fixed_array(float_to_rational(mat.x.x, decimal_places),
			                        float_to_rational(mat.x.y, decimal_places),
			                        float_to_rational(mat.x.z, decimal_places),

			                        float_to_rational(mat.y.x, decimal_places),
			                        float_to_rational(mat.y.y, decimal_places),
			                        float_to_rational(mat.y.z, decimal_places),

			                        float_to_rational(mat.z.x, decimal_places),
			                        float_to_rational(mat.z.y, decimal_places),
			                        float_to_rational(mat.z.z, decimal_places));
		};

		auto dng_primaries = scene_primaries;
		dng_primaries.w    = lak::col::cie::D65_xy;
		dng_primaries.regenerate_Y();

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
		ifd0.push_Model(lak::astring_view(
		  (const char *)(u8"RYE + "_str + raw_image->camera.model).c_str()));
		ifd0.push_Orientation(lak::fixed_array((uint16_t(1U))));
		ifd0.push_SamplesPerPixel(lak::fixed_array(uint16_t(3U)));

		ifd0.push_XResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_YResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_ResolutionUnit(lak::fixed_array(uint16_t(2U)));
		ifd0.push_Software(APP_NAME ""_view);
		ifd0.push_WhitePoint(lak::fixed_array(
		  lak::tiff::urational{uint32_t(dng_primaries.w.x * 1000.f), 1000U},
		  lak::tiff::urational{uint32_t(dng_primaries.w.y * 1000.f), 1000U}));
		ifd0.push_PrimaryChromaticities(lak::fixed_array(
		  lak::tiff::urational{uint32_t(dng_primaries.g.x * 1000.f), 1000U},
		  lak::tiff::urational{uint32_t(dng_primaries.g.y * 1000.f), 1000U},
		  lak::tiff::urational{uint32_t(dng_primaries.b.x * 1000.f), 1000U},
		  lak::tiff::urational{uint32_t(dng_primaries.b.y * 1000.f), 1000U},
		  lak::tiff::urational{uint32_t(dng_primaries.r.x * 1000.f), 1000U},
		  lak::tiff::urational{uint32_t(dng_primaries.r.y * 1000.f), 1000U}));
		ifd0.push_SampleFormat({1U});

		if (raw_image->shutter != 0.f)
			ifd0.push_ExposureTime(lak::fixed_array(lak::tiff::urational{
			  1000U, uint32_t((1.f / raw_image->shutter) * 1000)}));
		ifd0.push_FNumber(lak::fixed_array(
		  lak::tiff::urational{uint32_t(raw_image->aperture * 100), 100U}));
		ifd0.push_ISOSpeedRatings(lak::fixed_array(uint16_t(raw_image->iso)));
		ifd0.push_LightSource(lak::fixed_array(uint16_t(21U)));
		ifd0.push_FocalLength(lak::fixed_array(
		  lak::tiff::urational{uint32_t(raw_image->focal_length * 100), 100U}));

		ifd0.push_DNGVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_DNGBackwardVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_UniqueCameraModel(lak::string_view(lak::fmt<"RYE + {} {}">(
		  raw_image->camera.make, raw_image->camera.model)));
		ifd0.push_BlackLevel(lak::fixed_array(uint16_t(0)));
		ifd0.push_WhiteLevel(lak::fixed_array(uint16_t(UINT16_MAX)));
		ifd0.push_DefaultCropOrigin(lak::fixed_array(uint32_t(0U), uint32_t(0U)));
		ifd0.push_DefaultCropSize(
		  lak::fixed_array(uint32_t(img16.size().x), uint32_t(img16.size().y)));
		const auto iden_matrix =
		  mat_to_rational(dng_primaries.XYZ_to_linear(), 6U);
		ifd0.push_ColorMatrix1(iden_matrix);
		ifd0.push_ColorMatrix2(iden_matrix);
		ifd0.push_AsShotNeutral(
		  lak::fixed_array(uint16_t(1), uint16_t(1), uint16_t(1)));
		ifd0.push_CameraSerialNumber(
		  lak::astring_view((const char *)raw_image->camera.serial.c_str()));
		// same values as LightSource
		ifd0.push_CalibrationIlluminant1(lak::fixed_array(uint16_t(21U)));
		ifd0.push_CalibrationIlluminant2(lak::fixed_array(uint16_t(21U)));
		ifd0.push_ActiveArea(lak::fixed_array(uint32_t(0U),
		                                      uint32_t(0U),
		                                      uint32_t(img16.size().y),
		                                      uint32_t(img16.size().x)));

		auto &exif = ifd0.push_exif();

		exif.push_LensMake(
		  lak::astring_view((const char *)raw_image->lens.make.c_str()));
		exif.push_LensModel(
		  lak::astring_view((const char *)raw_image->lens.model.c_str()));
		exif.push_LensSerialNumber(
		  lak::astring_view((const char *)raw_image->lens.serial.c_str()));

		strm.write<lak::endian::native>(tiff).UNWRAP();

		if (!lak::save_file(path, strm.data)) ERROR("Failed to save DNG");
	}

	const lak::fs::path &file_path() { return binary_path; }

	bool update() { return binary_update || raw_update; }

	lak::path_getter open_pgetter, save_png_pgetter, save_dng_pgetter;

	void file_menu()
	{
		if (auto res = open_pgetter(); res) open_file(*res);
		if (auto res = save_png_pgetter(); res) save_png_file(*res);
		if (auto res = save_dng_pgetter(); res) save_dng_file(*res);

		if (ImGui::BeginMenu("File"))
		{
			const bool can_save = can_save_gpu_image();
			if (ImGui::MenuItem("Open...", nullptr, false))
				open_pgetter.open_file(
				  file_path(),
				  "Raw Image Files{.ARW,.RAF,.NEF,.CR3,.CR2,.DNG,.X3F},.*");

			if (ImGui::MenuItem("Save as DNG...", nullptr, false, can_save))
			{
				save_dng_pgetter.save_file(
				  file_path().parent_path() /
				    (file_path().stem().u8string() + u8"_RYE.DNG"),
				  "Image Files{.DNG}");
			}

			if (ImGui::MenuItem("Save as PNG...", nullptr, false, can_save))
			{
				save_png_pgetter.save_file(
				  file_path().parent_path() /
				    (file_path().stem().u8string() + u8"_RYE.PNG"),
				  "Image Files{.PNG}");
			}

			if (auto path = file_path().parent_path() /
			                (file_path().stem().u8string() + u8"_RYE.DNG");
			    ImGui::MenuItem(
			      "Quick save DNG", path.string().c_str(), false, can_save))
			{
				save_dng_file(path);
			}

			if (auto path = file_path().parent_path() /
			                (file_path().stem().u8string() + u8"_RYE.PNG");
			    ImGui::MenuItem(
			      "Quick save PNG", path.string().c_str(), false, can_save))
			{
				save_png_file(path);
			}

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
	float colour_temp   = 5000;
	float exposure      = 0.f;
	float lightness     = 0.f;
	float contrast      = 0.f;
	float saturation    = 0.f;
	float hue           = 0.f;
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

	lak::ImUniqueViewport image_viewport;
	lak::optional<rye_image_proc_state> image_viewport_state;

	lak::mat3f_t XYZ_to_cam;
	lak::mat3f_t cam_to_XYZ;
	lak::mat3f_t cam_to_sRGB;
	lak::vec4f_t wb_coef;
	lak::mat3f_t raw_wb;
	lak::mat4f_t user_mat = lak::diagonal(lak::vec4f_t(1.f));

	lak::col::cie::xyY_primaries camera_primaries =
	  lak::col::ProPhotoRGB_primaries;
	lak::col::cie::xyY_primaries scene_primaries =
	  lak::col::ProPhotoRGB_primaries;
	lak::col::cie::xy user_white_point = lak::col::ProPhotoRGB_primaries.w;
	lak::col::cie::xyY_primaries display_primaries = lak::col::sRGB_primaries;
	float display_gamma                            = 2.2f;

	enum struct colour_space_method : int
	{
		R_G_B     = 0, // Plain RGB
		IR_R_G    = 1, // Digital false colour IR
		R_G_B_neg = 2, // Colour negative film
		BW_neg    = 3, // B&W negative film
	};

	colour_space_method colour_method = colour_space_method::R_G_B;

	enum struct bw_source_channel : int
	{
		W_channel = 0,
		R_channel = 1,
		G_channel = 2,
		B_channel = 3,
	};

	bw_source_channel bw_source = bw_source_channel::R_channel;

	void calculate_camera_colour_space()
	{
		// switch (colour_method)
		// {
		// 	case colour_space_method::R_G_B:
		// 	{
		// 		auto prim = lak::transpose(cam_to_XYZ);
		// 		camera_primaries.r =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
		// 		camera_primaries.g =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
		// 		camera_primaries.b =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
		// 	}
		// 	break;
		// 	case colour_space_method::IR_R_G:
		// 	{
		// 		auto prim = lak::transpose(lak::inverse(XYZ_to_cam));
		// 		camera_primaries.r =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
		// 		camera_primaries.g =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
		// 		camera_primaries.b =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
		// 	}
		// 	break;
		// }
		auto prim = lak::transpose(cam_to_XYZ);
		camera_primaries.r =
		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
		camera_primaries.g =
		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
		camera_primaries.b =
		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
		camera_primaries.regenerate_w();
		camera_primaries.regenerate_Y();
	}

	void calculate_camera_white_balance()
	{
		switch (colour_method)
		{
			case colour_space_method::R_G_B_neg:
				[[fallthrough]];
			case colour_space_method::BW_neg:
				[[fallthrough]];
			case colour_space_method::R_G_B:
			{
				// colour_temp = std::min(std::max(colour_temp, 4000.f), 25000.f);
				// auto cct    = std::min(
				//   std::max(lak::col::cie::D_series_CCT(colour_temp), 4000.f),
				//   25000.f);
				// auto D50_cam = XYZ_to_cam * lak::col::cie::D50_XYZ.to_vec();
				// auto temp_cam =
				//   XYZ_to_cam * lak::col::cie::to_XYZ(
				//                  lak::col::cie::to_xyY(
				//                    lak::col::cie::D_series_illuminant(cct), 1.f))
				//                  .to_vec();
				// auto cct_wb = wb_coef.xyz() * (D50_cam / temp_cam);
				// raw_wb      = lak::diagonal(cct_wb);
				raw_wb = lak::diagonal(lak::vec3f_t(1.f));
			}
			break;

			case colour_space_method::IR_R_G:
			{
				raw_wb = lak::mat3f_t{
				  lak::vec3f_t(0.f, 0.f, 1.f / ir_balance.ir_in.b),
				  lak::vec3f_t(1.f, 0.f, -(ir_balance.ir_in.r / ir_balance.ir_in.b)),
				  lak::vec3f_t(0.f, 1.f, -(ir_balance.ir_in.g / ir_balance.ir_in.b)),
				};

				// // camera sensitivity compensation
				// const lak::vec3f_t aero_match_balance =
				//   rye::white_balance({1.f / ir_balance.aero_match.r,
				//                       1.f / ir_balance.aero_match.g,
				//                       1.f / ir_balance.aero_match.b});

				// // aerochrome sensitivity factor
				// const lak::vec3f_t aerochrome_sensitivity{
				//   std::exp(0.5f), std::exp(1.5f), std::exp(1.4f)};
				// const lak::vec3f_t aero_balance =
				//   aero_match_balance * rye::white_balance(aerochrome_sensitivity);
				// raw_wb.x *= aero_balance.x;
				// raw_wb.y *= aero_balance.y;
				// raw_wb.z *= aero_balance.z;

				// raw_wb = lak::transpose(raw_wb);

				// // blackbody whitebalance
				// auto wb_wv      = rye::relative_blackbody(colour_temp);
				// const auto g_wb = 1.0 / wb_wv(550.0);
				// const auto r_wb = 1.0 / wb_wv(660.0);
				// const auto i_wb = 1.0 / wb_wv(850.0);
				// const lak::vec3f_t temp_sensitivity{
				//   float(i_wb), float(r_wb), float(g_wb)};
				// const lak::vec3f_t temp_balance =
				// rye::white_balance(temp_sensitivity); raw_wb.x *= temp_balance.y;
				// raw_wb.y *= temp_balance.z;
				// raw_wb.z *= temp_balance.x;

				// raw_wb = lak::transpose(raw_wb);

				// DEBUG(lak::fmt<u8"raw_wb:\n{:-+9.3}\n{:-+9.3}\n{:-+9.3}">(
				//   raw_wb.x, raw_wb.y, raw_wb.z));
			}
			break;

			default:
				ASSERT_UNREACHABLE();
		}
	}

	void update_camera_colour_space()
	{
		image_viewport_state->bindings.template set_state_value<"camera_to_XYZ">(
		  lak::cobalt::from_lak(camera_primaries.linear_to_XYZ()));

		image_viewport_state->bindings
		  .template set_state_value<"camera_XYZ_to_scene_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaptation_matrix(
		      camera_primaries.w, scene_primaries.w)));

		image_viewport_state->bindings
		  .template set_state_value<"user_adaptation_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaptation_matrix(
		      user_white_point, camera_primaries.w)));

		run_compute = true;
	}

	void update_scene_colour_space()
	{
		image_viewport_state->bindings.template set_state_value<"scene_to_XYZ">(
		  lak::cobalt::from_lak(scene_primaries.linear_to_XYZ()));

		image_viewport_state->bindings.template set_state_value<"XYZ_to_scene">(
		  lak::cobalt::from_lak(scene_primaries.XYZ_to_linear()));

		image_viewport_state->bindings.template set_state_value<"scene_white_XYZ">(
		  lak::cobalt::from_lak(scene_primaries.w_XYZ().to_vec()));

		image_viewport_state->bindings
		  .template set_state_value<"camera_XYZ_to_scene_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaptation_matrix(
		      camera_primaries.w, scene_primaries.w)));

		run_compute = true;
	}

	void update_display_colour_space()
	{
		image_viewport_state->bindings.template set_state_value<"XYZ_to_display">(
		  lak::cobalt::from_lak(display_primaries.XYZ_to_linear()));

		image_viewport_state->bindings
		  .template set_state_value<"scene_XYZ_to_display_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaptation_matrix(
		      scene_primaries.w, display_primaries.w)));

		image_viewport_state->bindings.template set_state_value<"display_gamma">(
		  display_gamma);

		run_compute = true;
	}

	void update_camera_white_balance()
	{
		calculate_camera_white_balance();

		image_viewport_state->bindings
		  .template set_state_value<"raw_white_balance">(
		    lak::cobalt::from_lak(raw_wb));

		run_compute = true;
	}

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
					binary_path   = load_path;
					binary_update = true;
					raw_update    = true;
					time_acc      = 0.f;

					image_viewport_state.reset();
					reset_textures();

					ir_histo.clear();
					white_histo.clear();
					srgb_histo.clear();

					load_db_data();

					image_viewport.reset();
					finaltex.emplace(raw_image->data);

					image_viewport_state =
					  rye_image_proc_state::make(
					    window(), ImGui::ImplGetCobaltTexture(finaltex.get().GetTexID()))
					    .UNWRAP();

					wb_coef = lak::vec4f_t(raw_image->whitebalance_coef, 0.f);
					raw_wb  = lak::diagonal(wb_coef.xyz());

					XYZ_to_cam  = raw_image->XYZ_to_cam;
					cam_to_XYZ  = raw_image->cam_to_XYZ;
					cam_to_sRGB = raw_image->cam_to_sRGB;

					calculate_camera_colour_space();
					calculate_camera_white_balance();
					scene_primaries   = lak::col::ProPhotoRGB_primaries;
					display_primaries = lak::col::sRGB_primaries;

					// if (image_viewport_state->XYZ_to_display !=
					//     cobalt::graphics::StateValueId::Null)
					// {
					// 	auto &col = lraw->imgdata.color;
					// 	auto mat  = lak::mat4f_t{
					//     lak::vec4f_t(col.rgb_cam[0][0],
					//                  col.rgb_cam[0][1],
					//                  col.rgb_cam[0][2],
					//                  col.rgb_cam[0][3]),
					//     lak::vec4f_t(col.rgb_cam[1][0],
					//                  col.rgb_cam[1][1],
					//                  col.rgb_cam[1][2],
					//                  col.rgb_cam[1][3]),
					//     lak::vec4f_t(col.rgb_cam[2][0],
					//                  col.rgb_cam[2][1],
					//                  col.rgb_cam[2][2],
					//                  col.rgb_cam[2][3]),
					//     lak::vec4f_t(0.f, 0.f, 0.f, 1.f),
					//   };
					// 	image_viewport_state->state_group_node->SetStateValue(
					// 	  image_viewport_state->XYZ_to_display,
					// 	  lak::cobalt::from_lak(mat));
					// }

					update_camera_colour_space();
					update_scene_colour_space();
					update_display_colour_space();
					update_camera_white_balance();

					image_viewport_state->bindings
					  .template set_state_value<"user_matrix">(
					    lak::cobalt::from_lak(user_mat));

					image_viewport_state->bindings.template set_state_value<"exposure">(
					  exposure);

					image_viewport_state->bindings.template set_state_value<"contrast">(
					  contrast);

					image_viewport_state->bindings.template set_state_value<"lightness">(
					  lightness);

					image_viewport_state->bindings
					  .template set_state_value<"saturation">(saturation);

					image_viewport_state->bindings.template set_state_value<"hue">(hue);

					// image_viewport_state->bindings
					//   .template
					//   set_state_value<"camera_white_to_display_white_XYZ">(
					//     lak::cobalt::from_lak(display_primaries.w_XYZ().to_vec()));

					// if (image_viewport_state->temperature !=
					//     cobalt::graphics::StateValueId::Null)
					// {
					// 	auto wb_wv = rye::relative_blackbody(colour_temp);
					// 	const lak::vec3f_t temp_sensitivity{
					// 	  wb_wv(850.0), wb_wv(600.0), wb_wv(525.0)};
					// 	const lak::vec3f_t temp_balance =
					// 	  rye::white_balance(temp_sensitivity);
					// 	image_viewport_state->state_group_node->SetStateValue(
					// 	  image_viewport_state->temperature,
					// 	  lak::cobalt::from_lak(temp_balance));
					// }
					run_compute = true;
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
		else if (image_process && image_process->has_value())
		{
			image_process.reset();
			ir_histo    = lak::move(_ir_histo);
			white_histo = lak::move(_white_histo);
			srgb_histo  = lak::move(_srgb_histo);
			// lrawtex.emplace(raw_image->data);
			// lrprocessedtex.emplace(lrpimg);
			// lrdebayertex.emplace(lrdimg);
			// lrsrgbtex.emplace(lrsrgbimg);
			// lrwavetex.emplace(lrwaveimg);
			// lrwave2tex.emplace(lrwave2img);
			image_viewport_state.reset();
			image_viewport.reset();
		}
		else if (raw_update && !image_process)
		{
			raw_update = false;
			desqueeze  = std::max(.5f, std::min(10.f, desqueeze));
			image_process.reset();
			// process_image_async(lraw_white_level,
			//                     ir_balance,
			//                     colour_temp,
			//                     exposure,
			//                     lightness,
			//                     contrast,
			//                     saturation,
			//                     anamorphic ? lak::make_optional(desqueeze)
			//                                : lak::nullopt);
		}
		else if (image_viewport_state)
		{
			// if (run_compute)
			// {
			// 	lak::cobalt_append_compute_pass(window().handle(),
			// 	                                image_viewport_state->pass_node.get())
			// 	  .UNWRAP();
			// 	run_compute = false;
			// }

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
				lak::Text<u8"ISO {:.0} 1/{:.0}s f/{:.1} {:.1}mm ({:.1}mm)">(
				  raw_image->iso,
				  1.0f / raw_image->shutter,
				  raw_image->aperture,
				  raw_image->focal_length,
				  raw_image->focal_length_35mm == 0.f ? raw_image->focal_length
				                                      : raw_image->focal_length_35mm);

				ImGui::Separator();

				bool update_camera_primaries  = false;
				bool update_scene_primaries   = false;
				bool update_user_adapt        = false;
				bool update_raw_white_balance = false;
				bool update_user_mat          = false;
				{
					const char *labels[4] = {
					  "R G B",
					  "IR R G (Digital)",
					  "R G B negative (Film)",
					  "B&W negative (Film)",
					};
					if (ImGui::Combo("Colour mode",
					                 reinterpret_cast<int *>(&colour_method),
					                 labels,
					                 4))
					{
						calculate_camera_colour_space();
						update_camera_primaries  = true;
						update_raw_white_balance = true;
						update_user_mat          = true;
					}
				}

				if (colour_method == colour_space_method::BW_neg)
				{
					const char *labels[4] = {
					  "All channels",
					  "Red channel",
					  "Green channel",
					  "Blue channel",
					};
					update_user_mat |= ImGui::Combo(
					  "BW source", reinterpret_cast<int *>(&bw_source), labels, 4);
				}

				if (update_user_mat)
				{
					switch (colour_method)
					{
						case colour_space_method::R_G_B_neg:
						{
							user_mat = lak::diagonal(lak::vec4f_t(lak::vec3f_t(-1.f), 1.f));
							user_mat.x.w = 1.f;
							user_mat.y.w = 1.f;
							user_mat.z.w = 1.f;
						}
						break;

						case colour_space_method::BW_neg:
						{
							lak::vec4f_t basis;
							switch (bw_source)
							{
								default:
									[[fallthrough]];
								case bw_source_channel::W_channel:
									basis = lak::vec4f_t(lak::vec3f_t(-1.f), 3.f);
									break;
								case bw_source_channel::R_channel:
									basis = lak::vec4f_t(-1.f, 0.f, 0.f, 1.f);
									break;
								case bw_source_channel::G_channel:
									basis = lak::vec4f_t(0.f, -1.f, 0.f, 1.f);
									break;
								case bw_source_channel::B_channel:
									basis = lak::vec4f_t(0.f, 0.f, -1.f, 1.f);
									break;
							}
							user_mat = lak::mat4f_t{
							  basis,
							  basis,
							  basis,
							  lak::vec4f_t(lak::vec3f_t(0.f), 1.f),
							};
						}
						break;

						default:
							user_mat = lak::diagonal(lak::vec4f_t(1.f));
							break;
					}

					image_viewport_state->bindings
					  .template set_state_value<"user_matrix">(
					    lak::cobalt::from_lak(user_mat));
					run_compute = true;
				}

				ImGui::Separator();

				if (colour_method == colour_space_method::IR_R_G)
				{
					ImGui::Text("Raw IR white point");

					if (!use_database_ir_balance || !used_ir_balance_from_db)
					{
						update_raw_white_balance |= ImGui::DragFloat(
						  "R##IR in", &ir_balance.ir_in.r, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
						update_raw_white_balance |= ImGui::DragFloat(
						  "G##IR in", &ir_balance.ir_in.g, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
						update_raw_white_balance |= ImGui::DragFloat(
						  "B##IR in", &ir_balance.ir_in.b, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
					}
					else
					{
						ImGui::Text(
						  "IR in R: %.3f/%.3f", ir_balance.ir_in.r, ir_balance.ir_in.b);
						ImGui::Text(
						  "IR in G: %.3f/%.3f", ir_balance.ir_in.g, ir_balance.ir_in.b);
					}

					ImGui::Separator();

					// currently unused
					// ImGui::Text("Raw RGB white point");

					// if (!use_database_aero_match || !used_aero_match_from_db)
					// {
					// 	update_raw_white_balance |=
					// 	  ImGui::DragFloat("R##W",
					// 	                   &ir_balance.aero_match.r,
					// 	                   0.001f,
					// 	                   0.001f,
					// 	                   2.0f,
					// 	                   "IR/%.3f");
					// 	update_raw_white_balance |=
					// 	  ImGui::DragFloat("G##W",
					// 	                   &ir_balance.aero_match.g,
					// 	                   0.001f,
					// 	                   0.001f,
					// 	                   2.0f,
					// 	                   "R/%.3f");
					// 	update_raw_white_balance |=
					// 	  ImGui::DragFloat("B##W",
					// 	                   &ir_balance.aero_match.b,
					// 	                   0.001f,
					// 	                   0.001f,
					// 	                   2.0f,
					// 	                   "G/%.3f");
					// }
					// else
					// {
					// 	ImGui::Text("R: IR/%.3f\nG: R/%.3f\nB: G/%.3f",
					// 	            ir_balance.aero_match.r,
					// 	            ir_balance.aero_match.g,
					// 	            ir_balance.aero_match.b);
					// }

					ImGui::Separator();
				}
				else if (colour_method == colour_space_method::R_G_B_neg)
				{
					if (auto base_colour =
					      lak::vec3f_t(-user_mat.x.x, -user_mat.y.y, -user_mat.z.z);
					    ImGui::DragFloat3(
					      "Base colour", &base_colour.x, 0.05f, 0.f, 100.f))
					{
						user_mat     = lak::diagonal(lak::vec4f_t(-base_colour, 1.f));
						user_mat.x.w = 1.f;
						user_mat.y.w = 1.f;
						user_mat.z.w = 1.f;
						image_viewport_state->bindings
						  .template set_state_value<"user_matrix">(
						    lak::cobalt::from_lak(user_mat));
						run_compute = true;
					}
					ImGui::Separator();
				}
				else if (colour_method == colour_space_method::R_G_B)
				{
					bool update_user = false;

					update_user |= ImGui::DragFloat4(
					  "R##user_matrix_x", &user_mat.x.x, 0.05f, -10.f, 10.f);
					update_user |= ImGui::DragFloat4(
					  "G##user_matrix_y", &user_mat.y.x, 0.05f, -10.f, 10.f);
					update_user |= ImGui::DragFloat4(
					  "B##user_matrix_z", &user_mat.z.x, 0.05f, -10.f, 10.f);
					update_user |= ImGui::DragFloat4(
					  "H##user_matrix_w", &user_mat.w.x, 0.05f, -10.f, 10.f);

					if (update_user)
					{
						image_viewport_state->bindings
						  .template set_state_value<"user_matrix">(
						    lak::cobalt::from_lak(user_mat));
						run_compute = true;
					}
					ImGui::Separator();
				}

#if 0
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

				ImGui::Separator();
#endif

				if (false)
				{
					update_raw_white_balance |= ImGui::DragFloat(
					  "Temperature",
					  &colour_temp,
					  10.f,
					  ((colour_method == colour_space_method::IR_R_G) ? 1000.f : 4000.f),
					  25000.f,
					  "%.0fK");

					if (ImPlot::BeginPlot("Temperature spectrum", ImVec2(-1, 250)))
					{
						DEFER(ImPlot::EndPlot());

						const int wavelength_min   = 200;
						const int wavelength_max   = 1000;
						const int wavelength_steps = 10;

						const auto peak_radiance = lak::blackbody_radiance(
						  std::min(std::max(lak::blackbody_peak_wavelength(colour_temp),
						                    double(wavelength_min)),
						           double(wavelength_max)),
						  colour_temp);

						ImPlot::SetupAxis(ImAxis_X1,
						                  "Wavelength (nm)",
						                  ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus |
						                    ImPlotAxisFlags_NoSideSwitch);
						ImPlot::SetupAxisLimits(ImAxis_X1,
						                        double(wavelength_min),
						                        double(wavelength_max),
						                        ImPlotCond_Always);

						ImPlot::SetupAxis(ImAxis_Y1,
						                  "Sun's radiance (W/sr/m^2/nm)",
						                  ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus |
						                    ImPlotAxisFlags_NoSideSwitch);
						ImPlot::SetupAxisLimits(
						  ImAxis_Y1, 0.0, peak_radiance, ImPlotCond_Always);

						ImPlot::SetupAxis(ImAxis_Y2,
						                  "Earth's irradiance (W/m^2/nm)",
						                  ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus |
						                    ImPlotAxisFlags_NoSideSwitch |
						                    ImPlotAxisFlags_Opposite);
						ImPlot::SetupAxisLimits(ImAxis_Y2,
						                        0.0,
						                        peak_radiance * lak::suns_steradian,
						                        ImPlotCond_Always);

						ImPlot::SetupLegend(ImPlotLocation_SouthWest);
						ImPlot::SetupFinish();

						// ---

						ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
						ImPlotGetter radiance_getter = [](int index,
						                                  void *data) -> ImPlotPoint
						{
							const auto *wnd = reinterpret_cast<const rye_window *>(data);
							const double x =
							  double((index * wavelength_steps) + wavelength_min);
							const double y = lak::blackbody_radiance(x, wnd->colour_temp);
							return {x, y};
						};

						ImPlot::PlotLineG("##blackbody",
						                  radiance_getter,
						                  this,
						                  (wavelength_max - wavelength_min) /
						                    wavelength_steps);
					}

					if (ImGui::Button("Reset to camera##raw white balance"))
					{
						colour_temp              = 5000.0f;
						update_raw_white_balance = true;
					}
					ImGui::Separator();
				}

				if (ImGui::DragFloat("Exposure", &exposure, 0.01f, 0.f, 1000.f))
				{
					image_viewport_state->bindings.template set_state_value<"exposure">(
					  exposure);
					run_compute = true;
				}

				if (ImGui::DragFloat("Contrast", &contrast, 0.1f, -1000.f, 1000.f))
				{
					image_viewport_state->bindings.template set_state_value<"contrast">(
					  contrast);
					run_compute = true;
				}

				if (ImGui::DragFloat("Lightness", &lightness, 0.1f, -1000.f, 1000.f))
				{
					image_viewport_state->bindings.template set_state_value<"lightness">(
					  lightness);
					run_compute = true;
				}

				if (ImGui::DragFloat("Saturation", &saturation, 0.1f, -1000.f, 1000.f))
				{
					image_viewport_state->bindings
					  .template set_state_value<"saturation">(saturation);
					run_compute = true;
				}

				// if (ImGui::DragFloat("Hue (degrees)", &hue, 1.f, -360.f, 360.f))
				// {
				// 	image_viewport_state->bindings.template
				// set_state_value<"hue">(hue);
				// run_compute = true;
				// }

				ImGui::Separator();

				{
					auto sz        = ImGui::GetContentRegionAvail().x;
					auto primaries = camera_primaries;
					primaries.w    = user_white_point;
					primaries.regenerate_Y();
					update_user_adapt |=
					  lak::ChromaticityEdit("White point", &primaries, ImVec2(sz, sz));
					user_white_point = primaries.w;
					if (ImGui::Button("Reset to camera##user"))
					{
						user_white_point  = camera_primaries.w;
						update_user_adapt = true;
					}
					if (update_user_adapt)
					{
						image_viewport_state->bindings
						  .template set_state_value<"user_adaptation_XYZ">(
						    lak::cobalt::from_lak(
						      lak::col::cie::bradford_adaptation_matrix(
						        user_white_point, camera_primaries.w)));
						run_compute = true;
					}
					lak::Text<u8"w: (x:{:-+9.3} y:{:-+.3})">(user_white_point.x,
					                                         user_white_point.y);
				}

				ImGui::Separator();

				LAK_TREE_NODE("Camera Colour Settings")
				{
					auto sz = ImGui::GetContentRegionAvail().x;
					update_camera_primaries |=
					  lak::ChromaticityEdit("Camera", &camera_primaries, ImVec2(sz, sz));
					if (ImGui::Button("Reset to camera##camera primaries"))
					{
						calculate_camera_colour_space();
						update_camera_primaries = true;
					}
					lak::Text<
					  u8"r: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "g: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "b: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "w: (x:{:-+9.3} y:{:-+.3})">(camera_primaries.r.x,
					                               camera_primaries.r.y,
					                               camera_primaries.r.Y,
					                               camera_primaries.g.x,
					                               camera_primaries.g.y,
					                               camera_primaries.g.Y,
					                               camera_primaries.b.x,
					                               camera_primaries.b.y,
					                               camera_primaries.b.Y,
					                               camera_primaries.w.x,
					                               camera_primaries.w.y);
				}

				LAK_TREE_NODE("Scene Colour Settings")
				{
					auto sz = ImGui::GetContentRegionAvail().x;
					update_scene_primaries |=
					  lak::ChromaticityEdit("Scene", &scene_primaries, ImVec2(sz, sz)) |
					  update_camera_primaries;
					if (ImGui::Button("ProPhoto RGB##scene"))
					{
						scene_primaries        = lak::col::ProPhotoRGB_primaries;
						update_scene_primaries = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("sRGB##scene"))
					{
						scene_primaries        = lak::col::sRGB_primaries;
						update_scene_primaries = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Adobe RGB##scene"))
					{
						scene_primaries        = lak::col::opRGB_primaries;
						update_scene_primaries = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Camera (RAW)##scene"))
					{
						auto prim = lak::transpose(cam_to_XYZ);
						scene_primaries.r =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
						scene_primaries.g =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
						scene_primaries.b =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
						scene_primaries.regenerate_w();
						scene_primaries.regenerate_Y();
						update_scene_primaries = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Camera (current)##scene"))
					{
						scene_primaries        = camera_primaries;
						update_scene_primaries = true;
					}
					if (update_scene_primaries)
					{
						update_scene_colour_space();
					}
					lak::Text<
					  u8"r: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "g: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "b: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "w: (x:{:-+9.3} y:{:-+.3})">(scene_primaries.r.x,
					                               scene_primaries.r.y,
					                               scene_primaries.r.Y,
					                               scene_primaries.g.x,
					                               scene_primaries.g.y,
					                               scene_primaries.g.Y,
					                               scene_primaries.b.x,
					                               scene_primaries.b.y,
					                               scene_primaries.b.Y,
					                               scene_primaries.w.x,
					                               scene_primaries.w.y);
				}

				LAK_TREE_NODE("Display Colour Settings")
				{
					auto sz            = ImGui::GetContentRegionAvail().x;
					bool update_output = lak::ChromaticityEdit(
					  "Output", &display_primaries, ImVec2(sz, sz));
					update_output |=
					  ImGui::DragFloat("Gamma", &display_gamma, 0.001f, 1.f, 3.f);
					if (ImGui::Button("ProPhoto##display"))
					{
						display_gamma     = 1.f;
						display_primaries = lak::col::ProPhotoRGB_primaries;
						update_output     = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("sRGB##display"))
					{
						display_gamma     = 2.2f;
						display_primaries = lak::col::sRGB_primaries;
						update_output     = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Adobe RGB##display"))
					{
						display_gamma     = 1.f;
						display_primaries = lak::col::opRGB_primaries;
						update_output     = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Camera (RAW)##display"))
					{
						display_gamma = 1.f;
						auto prim     = lak::transpose(cam_to_XYZ);
						display_primaries.r =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
						display_primaries.g =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
						display_primaries.b =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
						display_primaries.regenerate_w();
						display_primaries.regenerate_Y();
						update_output = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Camera (current)##display"))
					{
						display_gamma     = 1.f;
						display_primaries = camera_primaries;
						update_output     = true;
					}
					if (update_output) update_display_colour_space();
					lak::Text<
					  u8"r: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "g: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "b: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "w: (x:{:-+9.3} y:{:-+.3})">(display_primaries.r.x,
					                               display_primaries.r.y,
					                               display_primaries.r.Y,
					                               display_primaries.g.x,
					                               display_primaries.g.y,
					                               display_primaries.g.Y,
					                               display_primaries.b.x,
					                               display_primaries.b.y,
					                               display_primaries.b.Y,
					                               display_primaries.w.x,
					                               display_primaries.w.y);
				}

				if (update_camera_primaries | update_scene_primaries)
					update_camera_colour_space();
				if (update_raw_white_balance | update_scene_primaries)
					update_camera_white_balance();

				ImGui::EndChild();

				ImGui::SameLine();

				ImGui::BeginChild("ImgRight",
				                  {right_size, -1},
				                  true,
				                  ImGuiWindowFlags_NoSavedSettings);
#if 0
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
#endif
				// LAK_TREE_NODE("sRGB")

				// {
				// 	ImGui::PushID("sRGB");
				// 	DEFER(ImGui::PopID());

				// 	lak::Image("##view",
				// 	           (ImTextureID)(uintptr_t)&image_viewport_state
				// 	             ->display_image_buffer,
				// 	           image_view_pos,
				// 	           image_view_scale);
				// }

				{
					ImGui::PushID("sRGB");
					DEFER(ImGui::PopID());

					ImGui::DragFloat("Zoom", &image_view_scale, 0.01f, 0.1f, 10.0f);
					const auto size     = ImGui::GetContentRegionAvail();
					const auto tex_size = lak::vec2f_t(lak::TextureSize(finaltex));

					ImGui::SameLine();
					if (ImGui::Button("Fit"))
					{
						image_view_scale =
						  std::fmin(size.x / tex_size.x, size.y / tex_size.y);
						image_view_pos = ImVec2(0, 0);
					}
					ImGui::SameLine();
					if (ImGui::Button("25%")) image_view_scale = 0.25f;
					ImGui::SameLine();
					if (ImGui::Button("100%")) image_view_scale = 1.f;
					ImGui::SameLine();
					if (ImGui::Button("250%")) image_view_scale = 2.5f;

					if (!image_viewport) [[unlikely]]
						image_viewport.emplace(lak::ImTextureColourFormat::RGB,
						                       lak::ImTextureChannelFormat::U8);

					const auto cursor = ImGui::GetCursorScreenPos();
					{
						auto viewport_rect = ImVec4(
						  image_view_pos.x, image_view_pos.y, tex_size.x, tex_size.y);
						ImVec4 quad_uv;
						lak::PanZoomViewportBehaviour("##image_view",
						                              viewport_rect,
						                              image_view_scale,
						                              quad_uv,
						                              nullptr,
						                              size);
						image_view_pos.x = viewport_rect.x;
						image_view_pos.y = viewport_rect.y;

						image_viewport_state->display_bindings
						  .template set_state_value<"image_uv">(lak::cobalt::from_lak(
						    lak::vec4f_t(quad_uv.x, quad_uv.y, quad_uv.z, quad_uv.w)));
					}
					ImGui::SetCursorScreenPos(cursor);
					{
						auto vpd = *lak::BeginViewport(image_viewport.get(), size)
						              .template get<ImGui::ImplCoViewportDetails>();
						DEFER(lak::EndViewport(image_viewport.get()));

						// if (vpd.passes->empty())
						{
							vpd.passes->clear();
							vpd.owned_passes->clear();

							if (run_compute)
							{
								vpd.passes->push_back(image_viewport_state->pass_node.get());
								run_compute = false;
							}

							auto clear_pass = vpd.append_pass();
							clear_pass->SetAttachmentClearData(
							  cobalt::graphics::IFrameBuffer::AttachmentType::Color,
							  0,
							  cobalt::graphics::V4Float32{0.3125f, 0.3125f, 0.3125f, 1.0f});
							clear_pass->SetAttachmentClearData(
							  cobalt::graphics::IFrameBuffer::AttachmentType::Depth,
							  0,
							  cobalt::graphics::V4Float32{1.f, 1.f, 1.f, 1.f});

							vpd.append_pass(image_viewport_state->display_pass_node.get());
						}
					}
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

lak::optional<lak::cobalt_renderer_settings> forced_cobalt_mode;

lak::error_code<int> basic_program_preinit(lak::span<char *> args)
{
	if (!args.empty()) args = args.subspan(1U);

	if (args.size() == 1U)
	{
		if (args[0] == "--version"_str)
		{
			std::cout << APP_NAME << "\n";
			return lak::err_t{EXIT_SUCCESS};
		}
		else if (args[0] == "--help"_str)
		{
#ifdef LAK_OS_WINDOWS
			auto exestr = "rye.exe "_str;
#else
			auto exestr = "rye "_str;
#endif

			std::cout << exestr << "--help\n";
			std::cout << exestr << "--version\n";
			lak::array<lak::astring> renderers;
#ifdef LAK_ENABLE_COBALT_OGL4
			renderers.push_back("--opengl"_str);
#endif
#ifdef LAK_ENABLE_COBALT_VK
			renderers.push_back("--vulkan"_str);
#endif
#ifdef LAK_ENABLE_COBALT_D3D11
			renderers.push_back("--d3d11"_str);
#endif
#ifdef LAK_ENABLE_COBALT_D3D12
			renderers.push_back("--d3d12"_str);
#endif
#ifdef LAK_ENABLE_COBALT_MTL
			renderers.push_back("--metal"_str);
#endif
			std::cout << exestr << "["
			          << lak::join_strings<char>("|"_view, lak::span(renderers))
			          << "] "
			             "[--onlyerr] "
			             "[<filepath>]\n";

			return lak::err_t{EXIT_SUCCESS};
		}
	}

	lak::debugger.std_out(u8"", u8"" APP_NAME "\n");

	for (size_t arg = 0U; arg < args.size(); ++arg)
	{
		if (args[arg] == "--onlyerr"_str)
		{
			force_only_error = true;
		}
		else
#ifdef LAK_ENABLE_COBALT_OGL4
		  if (args[arg] == "--opengl"_str)
		{
			RES_TRY_ASSIGN(
			  forced_cobalt_mode =,
			  lak::cobalt_renderer_settings::preferred_ogl4(
			    lak::cobalt_renderer_settings::feature_set_t{
			      cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
			    .map_err(
			      [](lak::monostate) -> int
			      {
				      ERROR("Failed to get OpenGL4 renderer");
				      return EXIT_FAILURE;
			      }));
		}
		else
#endif
#ifdef LAK_ENABLE_COBALT_VK
		  if (args[arg] == "--vulkan"_str)
		{
			RES_TRY_ASSIGN(
			  forced_cobalt_mode =,
			  lak::cobalt_renderer_settings::preferred_vk(
			    lak::cobalt_renderer_settings::feature_set_t{
			      cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
			    .map_err(
			      [](lak::monostate) -> int
			      {
				      ERROR("Failed to get Vulkan renderer");
				      return EXIT_FAILURE;
			      }));
		}
		else
#endif
#ifdef LAK_ENABLE_COBALT_D3D11
		  if (args[arg] == "--d3d11"_str)
		{
			RES_TRY_ASSIGN(
			  forced_cobalt_mode =,
			  lak::cobalt_renderer_settings::preferred_d3d11(
			    lak::cobalt_renderer_settings::feature_set_t{
			      cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
			    .map_err(
			      [](lak::monostate) -> int
			      {
				      ERROR("Failed to get Direct3D11 renderer");
				      return EXIT_FAILURE;
			      }));
		}
		else
#endif
#ifdef LAK_ENABLE_COBALT_D3D12
		  if (args[arg] == "--d3d12"_str)
		{
			RES_TRY_ASSIGN(
			  forced_cobalt_mode =,
			  lak::cobalt_renderer_settings::preferred_d3d12(
			    lak::cobalt_renderer_settings::feature_set_t{
			      cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
			    .map_err(
			      [](lak::monostate) -> int
			      {
				      ERROR("Failed to get Direct3D12 renderer");
				      return EXIT_FAILURE;
			      }));
		}
		else
#endif
#ifdef LAK_ENABLE_COBALT_MTL
		  if (args[arg] == "--metal"_str)
		{
			RES_TRY_ASSIGN(
			  forced_cobalt_mode =,
			  lak::cobalt_renderer_settings::preferred_metal(
			    lak::cobalt_renderer_settings::feature_set_t{
			      cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
			    .map_err(
			      [](lak::monostate) -> int
			      {
				      ERROR("Failed to get Metal renderer");
				      return EXIT_FAILURE;
			      }));
		}
		else
#endif
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

	if_let_some (const auto &settings, forced_cobalt_mode)
	{
		rye_wnd =
		  basic_create_window<rye_window>(basic_window_cobalt_settings, settings)
		    .UNWRAP();
	}
	else
	{
		rye_wnd = basic_create_window<rye_window>(
		            basic_window_cobalt_settings,
		            lak::cobalt_renderer_settings::feature_set_t{
		              cobalt::graphics::IGraphicsDevice::Feature::ComputeShaders})
		            .UNWRAP();
	}

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
