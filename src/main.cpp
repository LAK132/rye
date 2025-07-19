#include "main.hpp"
#include "rye.hpp"

#include <lak/file.hpp>
#include <lak/future.hpp>
#include <lak/strconv.hpp>
#include <lak/tasks.hpp>
#include <lak/test.hpp>

#include <lak/opengl/state.hpp>

#include <stb_image_write.h>

#include <libraw/libraw.h>

#include <filesystem>

#include <inttypes.h>

#define LAK_BASIC_PROGRAM_IMGUI_WINDOW_IMPL
#include <lak/basic_single_window_program.inl>

#include <unordered_map>

int opengl_major, opengl_minor;
lak::graphics_mode graphics_mode;
bool force_only_error = false;

lak::fs::path binary_path;
lak::optional<lak::future<void>> binary_load, image_process;
lak::array<byte_t> binary;
bool binary_update = false, raw_update = false;

rye_texture lrawtex, lrdebayertex, lrsrgbtex, lrwavetex;

lak::optional<LibRaw> lraw;
lak::image<lak::vec3f_t> lrawimg, lrdimg, lrsrgbimg, lrwaveimg;

bool use_database_ir_balance = true;
bool use_database_aero_match = true;
bool used_ir_balance_from_db = false;
struct rye_ir_balance
{
	float ir_in_red   = 1.f;
	float ir_in_green = 1.f;
	lak::vec3f_t aero_match{.35f, 1.4f, .7f};
};

std::unordered_map<lak::astring, rye_ir_balance> ir_balance_db = {
  {"Canon EOS M50"_str,
   {
     .ir_in_red   = 0.930f,
     .ir_in_green = 0.730f,
     .aero_match  = {.64f, 1.8f, .7f},
   }},
  {"Canon EOS M6 Mark II"_str,
   {
     .ir_in_red   = 1.07f,
     .ir_in_green = 0.9f,
     .aero_match  = {.42f, 1.8f, .59f},
   }},
  {"Fujifilm X-T30"_str,
   {
     .ir_in_red   = 1.018f,
     .ir_in_green = 0.978f,
     .aero_match  = {.6f, 1.8f, .7f},
   }},
  {"Nikon D5200"_str,
   {
     .ir_in_red   = 1.036f,
     .ir_in_green = 0.690f,
     .aero_match  = {.1f, 2.f, .7f},
   }},
  {"Sony ILCE-7RM2"_str,
   {
     .ir_in_red   = 1.030f,
     .ir_in_green = 1.073f,
     .aero_match  = {.35f, 1.4f, .7f},
   }},
};

std::ostream &operator<<(std::ostream &strm, LibRaw_errors err)
{
	return strm << libraw_strerror(err);
}

lak::error_code<LibRaw_errors> libraw_as_result(int code)
{
	if (code == LIBRAW_SUCCESS)
		return lak::ok_t{};
	else
		return lak::err_t{static_cast<LibRaw_errors>(code)};
}

lak::error_codes<lak::errno_error, LibRaw_errors> load_binary_ex(
  lak::fs::path path)
{
	RES_TRY_ASSIGN(binary =, lak::read_file(path));
	RES_TRY(libraw_as_result(lraw->open_buffer(binary.begin(), binary.size())));
	RES_TRY(libraw_as_result(lraw->unpack()));
	RES_TRY(libraw_as_result(lraw->raw2image()));
	RES_TRY(libraw_as_result(lraw->subtract_black()));
	RES_TRY(libraw_as_result(lraw->adjust_maximum()));
	binary_path = lak::move(path);
	return lak::ok_t{};
}

void load_binary(const lak::fs::path &path)
{
	if_let_err (auto err, load_binary_ex(path))
	{
		binary.clear();
		binary_path.clear();
		ERROR("Failed to load file: ", err);
	}
}

void load_binary_async(const lak::fs::path &path)
{
	binary_load = lak::async(load_binary, path);
}

void process_image(int white_level,
                   float ir_in_red,
                   float ir_in_green,
                   float colour_temp,
                   lak::vec3f_t aero_match,
                   float exposure,
                   float lightness,
                   float contrast,
                   float saturation)
{
	if (white_level < 0) white_level = 0;
	if (static_cast<unsigned int>(white_level) > lraw->imgdata.color.maximum)
		white_level = lraw->imgdata.color.maximum;

	auto wb_wv = [&colour_temp](double wavelength) -> float
	{
		const static double blackbody_max =
		  lak::blackbody_peak_radiance(colour_temp);
		return float(lak::blackbody_radiance(wavelength, colour_temp) /
		             blackbody_max);
	};

	auto wb = [](lak::vec3f_t point) -> lak::vec3f_t
	{
		float max = rye_vec_max(point);
		return {max / point.r, max / point.g, max / point.b};
	};

	lrawimg.resize({lraw->imgdata.sizes.iwidth, lraw->imgdata.sizes.iheight});

	lak::tasks tasks{lak::tasks::hardware_max()};

	// convert raw to float and apply white level adjustment
	for (size_t y = 0; y < lrawimg.size().y; ++y)
	{
		tasks.push(
		  [&, iy = y * lrawimg.size().x]()
		  {
			  for (size_t x = 0; x < lrawimg.size().x; ++x)
			  {
				  const size_t i = iy + x;

				  lrawimg[i].r = float(lraw->imgdata.image[i][0]) / white_level;
				  lrawimg[i].g = float(lraw->imgdata.image[i][1]) / white_level;
				  lrawimg[i].b = float(lraw->imgdata.image[i][2]) / white_level;
			  }
		  });
	}
	tasks.await();

	bool is_foveon = lraw->imgdata.idata.is_foveon;
	bool is_xtrans = lraw->imgdata.idata.filters == 9U;

	const int flip = lraw->imgdata.sizes.flip;
	auto flip4     = [flip](lak::vec2s_t index) -> lak::vec2s_t
	{
		if (flip & 4) index = {index.y, index.x};
		return index;
	};
	auto flip12 = [flip](lak::vec2s_t index, lak::vec2s_t size) -> lak::vec2s_t
	{
		if (flip & 2) index.x = (size.x - 1U) - index.x;
		if (flip & 1) index.y = (size.y - 1U) - index.y;
		return index;
	};
	auto flip124 = [&](lak::vec2s_t index, lak::vec2s_t size) -> lak::vec2s_t
	{ return flip12(flip4(index), size); };

	lak::vec2s_t isize{lraw->imgdata.sizes.iwidth, lraw->imgdata.sizes.iheight};

	// downscale debayer
	if (is_foveon)
	{
		ASSERT_NYI();

#if 0
		lrdimg.resize(flip4(isize));
#endif
	}
	else if (is_xtrans)
	{
		isize.x /= 3U;
		isize.y /= 3U;
		lrdimg.resize(flip4(isize));

		for (size_t y = 0; y < isize.y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  const size_t y3 = y * 3;
				  for (size_t x = 0; x < isize.x; ++x)
				  {
					  const size_t x3       = x * 3;
					  const lak::vec2s_t xy = flip124({x, y}, lrdimg.size());

					  const bool xtrans_even_cell = (x + y) % 2U == 0U;
					  const size_t xtrans_off1    = xtrans_even_cell ? 0U : 1U;
					  const size_t xtrans_off2    = xtrans_even_cell ? 1U : 0U;

					  lrdimg[xy].r = (lrawimg[{x3 + 2U, y3 + xtrans_off1}].r +
					                  lrawimg[{x3 + xtrans_off2, y3 + 2U}].r) /
					                 2.f;

					  lrdimg[xy].g =
					    (lrawimg[{x3, y3}].g + lrawimg[{x3 + 1U, y3}].g +
					     lrawimg[{x3, y3 + 1U}].g + lrawimg[{x3 + 1U, y3 + 1U}].g +
					     lrawimg[{x3 + 2U, y3 + 2U}].g) /
					    5.f;

					  lrdimg[xy].b = (lrawimg[{x3 + 2U, y3 + xtrans_off2}].b +
					                  lrawimg[{x3 + xtrans_off1, y3 + 2U}].b) /
					                 2.f;
				  }
			  });
		}
	}
	else
	{
		isize.x /= 2U;
		isize.y /= 2U;
		lrdimg.resize(flip4(isize));

		lak::vec2s_t channels[4U] = {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
		for (int r = 0; r < 2; ++r)
			for (int c = 0; c < 2; ++c)
				if (int col = lraw->COLOR(r, c); col <= 3)
					channels[size_t(col)] = {size_t(r), size_t(c)};

		for (size_t y = 0; y < isize.y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  const size_t y2 = y * 2;
				  for (size_t x = 0; x < isize.x; ++x)
				  {
					  const size_t x2       = x * 2;
					  const lak::vec2s_t xy = flip124({x, y}, lrdimg.size());
					  const lak::vec2s_t xy2{x2, y2};

					  lrdimg[xy].r = lrawimg[xy2 + channels[0U]].r;
					  lrdimg[xy].g = std::max(lrawimg[xy2 + channels[1U]].g,
					                          lrawimg[xy2 + channels[3U]].g);
					  lrdimg[xy].b = lrawimg[xy2 + channels[2U]].b;
				  }
			  });
		}
	}
	tasks.await();

	// generate waveform
	lrwaveimg.resize({lrdimg.size().x, 1000U});
	lrwaveimg.fill({0.f, 0.f, 0.f});
	const float waveform_step = 100.f / float(lrdimg.size().y);
	for (size_t y = 0; y < lrdimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < lrwaveimg.size().x; ++x)
			  {
				  auto clamp = [](float v) -> size_t
				  {
					  v = std::log10((v * 90.f) + 10.f) - 1.f;
					  if (v <= 0.f)
						  return 0U;
					  else if (v >= 1.f)
						  return 999U;

					  size_t res = static_cast<size_t>(v * 1000.f);
					  if (res >= 1000U)
						  return 999U;
					  else
						  return res;
				  };
				  lak::vec3f_t irgb = lrdimg[{x, y}];
				  irgb.g += irgb.b - (ir_in_green * irgb.b);
				  irgb.r += irgb.b - (ir_in_red * irgb.b);
				  lrwaveimg[{x, 999U - clamp(irgb.r)}].r += waveform_step;
				  lrwaveimg[{x, 999U - clamp(irgb.g)}].g += waveform_step;
				  lrwaveimg[{x, 999U - clamp(irgb.b)}].b += waveform_step;
			  }
		  });
	}
	tasks.await();

	// IR processing stage 1
	for (size_t y = 0; y < lrdimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < lrdimg.size().x; ++x)
			  {
				  const float ir   = lrdimg[{x, y}].b;
				  lrdimg[{x, y}].b = lrdimg[{x, y}].g - (ir_in_green * ir);
				  lrdimg[{x, y}].g = lrdimg[{x, y}].r - (ir_in_red * ir);
				  lrdimg[{x, y}].r = ir;
			  }
		  });
	}
	tasks.await();

	// IR processing stage 2
	for (size_t y = 0; y < lrdimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < lrdimg.size().x; ++x)
			  {
				  lak::vec3f_t &irrgb = lrdimg[{x, y}];

				  const float min = rye_vec_min<float>(irrgb);
				  if (min < 0.0f) irrgb -= {min, min, min};

				  lak::vec3f_t ir_wb =
				    wb({wb_wv(850E-9), wb_wv(600E-9), wb_wv(525E-9)});
				  [[maybe_unused]] lak::vec3f_t vis_wb =
				    wb({wb_wv(600E-9), wb_wv(525E-9), wb_wv(460E-9)});

				  irrgb *= ir_wb;
			  }
		  });
	}
	tasks.await();

	// IR processing stage 3
	for (size_t y = 0; y < lrdimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < lrdimg.size().x; ++x)
			  {
				  lrdimg[{x, y}] *= wb(aero_match);
			  }
		  });
	}
	tasks.await();

	// convert to sRGB
	lrsrgbimg.resize(lrdimg.size());
	for (size_t y = 0; y < lrsrgbimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < lrsrgbimg.size().x; ++x)
				  lrsrgbimg[{x, y}] = rye_to_srgb(rye_exp_correction(
				    lrdimg[{x, y}], exposure, lightness, contrast, saturation));
		  });
	}
}

void process_image_async(int white_level,
                         float ir_in_red,
                         float ir_in_green,
                         float colour_temp,
                         lak::vec3f_t aero_match,
                         float exposure,
                         float lightness,
                         float contrast,
                         float saturation)
{
	image_process = lak::async(process_image,
	                           white_level,
	                           ir_in_red,
	                           ir_in_green,
	                           colour_temp,
	                           aero_match,
	                           exposure,
	                           lightness,
	                           contrast,
	                           saturation);
}

struct main_window : lak::basic_window<main_window>
{
	using super_window = lak::basic_window<main_window>;

	static void open_file(const lak::fs::path &path) { load_binary_async(path); }

	static void save_png_file(const lak::fs::path &path,
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

	static void save_dng_file(const lak::fs::path &) { ASSERT_NYI(); }

	static const lak::fs::path &file_path() { return binary_path; }

	static lak::span<byte_t> file_data() { return lak::span(binary); }

	static lak::graphics_mode graphics_mode() { return ::graphics_mode; }

	static bool update() { return binary_update || raw_update; }

	static void file_menu()
	{
		static lak::path_getter open_pgetter, save_png_pgetter, save_dng_pgetter;
		static bool save_srgb_png = false;
		if (auto res = open_pgetter(); res) open_file(*res);
		if (auto res = save_png_pgetter(); res)
			save_png_file(*res, save_srgb_png ? lrsrgbimg : lrdimg);
		if (auto res = save_dng_pgetter(); res) save_dng_file(*res);

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open...", nullptr, false))
				open_pgetter.open_file(file_path(),
				                       "Raw Image Files{.ARW,.RAF,.NEF,.CR3,.CR2},.*");

			if (ImGui::MenuItem("Save PNG (for editing)...",
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

			if (ImGui::MenuItem("Save PNG (sRGB final)...",
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

	static void menu_bar(float)
	{
		file_menu();
		ImGui::Checkbox("Use database IR balance", &use_database_ir_balance);
		ImGui::Checkbox("Use database RGB sensitivity", &use_database_aero_match);
	}

	static void main_region(float frame_time)
	{
		static int lraw_white_level = UINT16_MAX;
		static rye_ir_balance ir_balance;

		if (binary_load)
		{
			ImGui::BeginChild(
			  "Mid", {-1, -1}, true, ImGuiWindowFlags_NoSavedSettings);
			static float time_acc = 0.0f;
			time_acc += frame_time;
			if (time_acc > 3.0f) time_acc -= std::trunc(time_acc);
			if (time_acc > 2.0f)
				ImGui::Text("Loading...");
			else if (time_acc > 1.0f)
				ImGui::Text("Loading..");
			else
				ImGui::Text("Loading.");

			if (binary_load->has_value())
			{
				binary_load.reset();
				binary_update = true;
				raw_update    = true;
				time_acc      = 0.0f;

				used_ir_balance_from_db = false;
				if (use_database_ir_balance || use_database_aero_match)
				{
					if (auto it = ir_balance_db.find(lraw->imgdata.idata.make + " "_str +
					                                 lraw->imgdata.idata.model);
					    it != ir_balance_db.end())
					{
						if (use_database_ir_balance)
						{
							ir_balance.ir_in_red    = it->second.ir_in_red;
							ir_balance.ir_in_green  = it->second.ir_in_green;
							used_ir_balance_from_db = true;
						}
						if (use_database_aero_match)
						{
							ir_balance.aero_match = it->second.aero_match;
						}
					}
				}
			}
			ImGui::EndChild();
		}
		else if (binary.empty())
		{
			ImGui::BeginChild(
			  "Mid", {-1, -1}, true, ImGuiWindowFlags_NoSavedSettings);
			ImGui::Text("No file");
			ImGui::EndChild();
		}
		else
		{
			static float lraw_contrast = 1.0f;
			static float colour_temp   = 5500;
			static float exposure      = 0.f;
			static float lightness     = 0.f;
			static float contrast      = 0.f;
			static float saturation    = 0.f;

			if (image_process && image_process->has_value())
			{
				image_process.reset();
				lrawtex      = rye_create_texture(lrawimg, graphics_mode());
				lrdebayertex = rye_create_texture(lrdimg, graphics_mode());
				lrsrgbtex    = rye_create_texture(lrsrgbimg, graphics_mode());
				lrwavetex    = rye_create_texture(lrwaveimg, graphics_mode());
			}

			if (raw_update && !image_process)
			{
				raw_update = false;
				process_image_async(lraw_white_level,
				                    ir_balance.ir_in_red,
				                    ir_balance.ir_in_green,
				                    colour_temp,
				                    ir_balance.aero_match,
				                    exposure,
				                    lightness,
				                    contrast,
				                    saturation);
			}

			{
				const auto content_size{ImGui::GetContentRegionAvail()};

				static float left_size  = std::min<float>(content_size.x / 2, 500.f);
				static float right_size = content_size.x - left_size;

				lak::VertSplitter(left_size, right_size, content_size.x);

				ImGui::BeginChild(
				  "ImgLeft", {left_size, -1}, true, ImGuiWindowFlags_NoSavedSettings);
				ImGui::Text("Make: '%s'", lraw->imgdata.idata.make);
				ImGui::Text("Model: '%s'", lraw->imgdata.idata.model);
				ImGui::Text("ISO %.0f 1/%.0fs f/%.0f %.0fmm",
				            lraw->imgdata.other.iso_speed,
				            1.0f / lraw->imgdata.other.shutter,
				            lraw->imgdata.other.aperture,
				            lraw->imgdata.other.focal_len);

				if (use_database_ir_balance && used_ir_balance_from_db)
				{
					ImGui::Text("IR in Red: %.3f", ir_balance.ir_in_red);
					ImGui::Text("IR in Green: %.3f", ir_balance.ir_in_green);
				}
				else
				{
					ImGui::DragFloat(
					  "IR in Red", &ir_balance.ir_in_red, 0.0001f, 0.1f, 2.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

					ImGui::DragFloat(
					  "IR in Green", &ir_balance.ir_in_green, 0.0001f, 0.1f, 2.0f);
					if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;
				}

				ImGui::SliderInt(
				  "White level", &lraw_white_level, 0, lraw->imgdata.color.maximum);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::Separator();

				ImGui::SliderFloat("Temperature", &colour_temp, 2000.f, 10000.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat3(
				  "RGB Sensitivity", &ir_balance.aero_match.r, 0.0f, 2.0f, "1/%.3f");
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::Separator();

				ImGui::SliderFloat("Exposure", &exposure, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat("Contrast", &contrast, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat("Lightness", &lightness, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat("Saturation", &saturation, -100.f, 100.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				if (image_process) ImGui::Text("Processing...");

				ImGui::EndChild();

				ImGui::SameLine();

				ImGui::BeginChild("ImgRight",
				                  {right_size, -1},
				                  true,
				                  ImGuiWindowFlags_NoSavedSettings);
				LAK_TREE_NODE("RAW")
				{
					static float lraw_size = 0.5f;
					rye_image_view(lrawtex, &lraw_size);
				}
				LAK_TREE_NODE("WAVEFORM")
				{
					static float lraw_size = 1.0f;
					rye_image_view(lrwavetex, &lraw_size);
				}
				LAK_TREE_NODE("DEBAYER")
				{
					static float lraw_size = 1.0f;
					rye_image_view(lrdebayertex, &lraw_size);
				}
				// LAK_TREE_NODE("sRGB")
				{
					static float lraw_size = 1.0f;
					rye_image_view(lrsrgbtex, &lraw_size);
				}
				ImGui::EndChild();
			}
		}
	}
};

lak::optional<int> basic_program_preinit(lak::span<char *> args)
{
	if (!args.empty()) args = args.subspan(1U);

	if (args.size() == 1U && args[0] == lak::astring("--version"))
	{
		std::cout << APP_NAME << "\n";
		return lak::optional<int>(0);
	}

	lak::debugger.std_out(u8"", u8"" APP_NAME "\n");

	for (size_t arg = 0U; arg < args.size(); ++arg)
	{
		if (args[arg] == lak::astring("-h") || args[arg] == lak::astring("--help"))
		{
			std::cout << "rye.exe "
			             "[--help] "
			             "[--nogl] "
			             "[--onlyerr] "
			             "[<filepath>]\n";

			return lak::optional<int>(0);
		}
		else if (args[arg] == lak::astring("--nogl"))
		{
			basic_window_force_software = true;
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

	return lak::nullopt;
}

lak::optional<int> basic_single_window_program_init()
{
	basic_window_target_framerate                = 30;
	basic_window_opengl_settings.major           = 3;
	basic_window_opengl_settings.minor           = 2;
	basic_window_opengl_settings.double_buffered = true;
	basic_window_clear_colour                    = {0.0f, 0.0f, 0.0f, 1.0f};

	basic_imgui_main_window_flags =
	  ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
	  ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings |
	  ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove;

	lraw.emplace();

	return lak::nullopt;
}

int basic_program_quit() { return EXIT_SUCCESS; }

void basic_window_init(lak::window &window)
{
	lak::debugger.crash_path =
	  std::filesystem::current_path() / "ATTACH-TO-ISSUE-ON-RYE-GITHUB-REPO.txt";

	lak::debugger.live_output_enabled = true;

	graphics_mode = window.graphics();

	DEBUG("Graphics: ", graphics_mode);
	if (!lak::debugger.live_output_enabled || lak::debugger.live_errors_only)
		std::cout << "Graphics: " << graphics_mode << "\n";

	switch (graphics_mode)
	{
		case lak::graphics_mode::OpenGL:
		{
			opengl_major = lak::opengl::get_uint(GL_MAJOR_VERSION).UNWRAP();
			opengl_minor = lak::opengl::get_uint(GL_MINOR_VERSION).UNWRAP();
		}
		break;

		default:
			break;
	}

	window.set_title(L"" APP_NAME);
}

void basic_window_handle_event(lak::window *window, lak::event &event)
{
	switch (event.type)
	{
		case lak::event_type::close_window:
			ASSERT(!!window);
			basic_destroy_window(*window);
			break;

		case lak::event_type::quit_program:
			// Need to rework this, causes a crash.
			// ASSERT(!!basic_single_window_window);
			// basic_destroy_window(*basic_single_window_window);
			break;

		case lak::event_type::dropfile:
			load_binary_async(lak::fs::path(event.dropfile().path));
			break;

		default:
			break;
	}
}

void basic_window_loop(lak::window &window, uint64_t counter_delta)
{
	const float frame_time = (float)counter_delta / lak::performance_frequency();

	main_window::draw(frame_time);

	if (binary_update)
	{
		window.set_title(L"" APP_NAME " (" + binary_path.generic_wstring() + L")");
		binary_update = false;
	}
}

void basic_window_quit(lak::window &)
{
	lraw.reset();
	lrawtex      = lak::monostate{};
	lrdebayertex = lak::monostate{};
	lrsrgbtex    = lak::monostate{};
	lrwavetex    = lak::monostate{};
}
