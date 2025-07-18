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

int opengl_major, opengl_minor;
lak::graphics_mode graphics_mode;
bool force_only_error = false;

lak::fs::path binary_path;
lak::optional<lak::future<void>> binary_load, image_process;
lak::array<byte_t> binary;
bool binary_update = false, raw_update = false;

rye_texture lrawtex, lrdebayertex;

lak::optional<LibRaw> lraw;
lak::image<lak::vec3f_t> lrawimg, lrdimg;

std::ostream &operator<<(std::ostream &strm, LibRaw_errors err)
{
	switch (err)
	{
		case LIBRAW_SUCCESS:
			strm << "LIBRAW_SUCCESS";
			break;
		case LIBRAW_UNSPECIFIED_ERROR:
			strm << "LIBRAW_UNSPECIFIED_ERROR";
			break;
		case LIBRAW_FILE_UNSUPPORTED:
			strm << "LIBRAW_FILE_UNSUPPORTED";
			break;
		case LIBRAW_REQUEST_FOR_NONEXISTENT_IMAGE:
			strm << "LIBRAW_REQUEST_FOR_NONEXISTENT_IMAGE";
			break;
		case LIBRAW_OUT_OF_ORDER_CALL:
			strm << "LIBRAW_OUT_OF_ORDER_CALL";
			break;
		case LIBRAW_NO_THUMBNAIL:
			strm << "LIBRAW_NO_THUMBNAIL";
			break;
		case LIBRAW_UNSUPPORTED_THUMBNAIL:
			strm << "LIBRAW_UNSUPPORTED_THUMBNAIL";
			break;
		case LIBRAW_INPUT_CLOSED:
			strm << "LIBRAW_INPUT_CLOSED";
			break;
		case LIBRAW_NOT_IMPLEMENTED:
			strm << "LIBRAW_NOT_IMPLEMENTED";
			break;
		case LIBRAW_REQUEST_FOR_NONEXISTENT_THUMBNAIL:
			strm << "LIBRAW_REQUEST_FOR_NONEXISTENT_THUMBNAIL";
			break;
		case LIBRAW_UNSUFFICIENT_MEMORY:
			strm << "LIBRAW_UNSUFFICIENT_MEMORY";
			break;
		case LIBRAW_DATA_ERROR:
			strm << "LIBRAW_DATA_ERROR";
			break;
		case LIBRAW_IO_ERROR:
			strm << "LIBRAW_IO_ERROR";
			break;
		case LIBRAW_CANCELLED_BY_CALLBACK:
			strm << "LIBRAW_CANCELLED_BY_CALLBACK";
			break;
		case LIBRAW_BAD_CROP:
			strm << "LIBRAW_BAD_CROP";
			break;
		case LIBRAW_TOO_BIG:
			strm << "LIBRAW_TOO_BIG";
			break;
		case LIBRAW_MEMPOOL_OVERFLOW:
			strm << "LIBRAW_MEMPOOL_OVERFLOW";
			break;
		default:
			strm << "unknown libraw error";
			break;
	}
	return strm;
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

void process_image(int black_level,
                   int white_level,
                   float ir_in_red,
                   float ir_in_green,
                   float colour_temp,
                   lak::vec3f_t aero_match)
{
	if (black_level < 0) black_level = 0;
	if (static_cast<unsigned int>(black_level) > lraw->imgdata.color.maximum)
		black_level = lraw->imgdata.color.maximum;
	if (white_level < 0) white_level = 0;
	if (static_cast<unsigned int>(white_level) > lraw->imgdata.color.maximum)
		white_level = lraw->imgdata.color.maximum;

	lrawimg.resize({lraw->imgdata.sizes.iwidth, lraw->imgdata.sizes.iheight});
	lrdimg.resize(
	  {lraw->imgdata.sizes.iwidth / 2U, lraw->imgdata.sizes.iheight / 2U});

	lak::tasks tasks{lak::tasks::hardware_max()};
	for (size_t y = 0; y < lrawimg.size().y; ++y)
	{
		tasks.push(
		  [&, iy = y * lrawimg.size().x]()
		  {
			  for (size_t x = 0; x < lrawimg.size().x; ++x)
			  {
				  const size_t i = iy + x;

				  lrawimg[i].r = (float(lraw->imgdata.image[i][0] - black_level) /
				                  (white_level - black_level));
				  lrawimg[i].g = (float(lraw->imgdata.image[i][1] - black_level) /
				                  (white_level - black_level));
				  lrawimg[i].b = (float(lraw->imgdata.image[i][2] - black_level) /
				                  (white_level - black_level));
			  }
		  });
	}
	tasks.await();

	for (size_t y = 0; y < lrdimg.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  const size_t y2 = y * 2;
			  for (size_t x = 0; x < lrdimg.size().x; ++x)
			  {
				  const size_t x2 = x * 2;

				  const float ir    = lrawimg[{x2 + 1U, y2 + 1U}].b;
				  const float red   = lrawimg[{x2, y2}].r - (ir_in_red * ir);
				  const float green = lrawimg[{x2 + 1U, y2}].g - (ir_in_green * ir);
				  lak::vec3f_t irrgb{ir, red, green};

				  const float min = rye_vec_min<float>(irrgb);
				  if (min < 0.0f) irrgb -= {min, min, min};

				  auto wb_wv = [&](double wavelength) -> float
				  {
					  const static double blackbody_max =
					    lak::blackbody_peak_radiance(colour_temp);
					  return float(lak::blackbody_radiance(wavelength, colour_temp) /
					               blackbody_max);
				  };

				  auto wb = [&](lak::vec3f_t point) -> lak::vec3f_t
				  {
					  float max = rye_vec_max(point);
					  return {max / point.r, max / point.g, max / point.b};
				  };

				  lak::vec3f_t ir_wb =
				    wb({wb_wv(850E-9), wb_wv(600E-9), wb_wv(525E-9)});
				  [[maybe_unused]] lak::vec3f_t vis_wb =
				    wb({wb_wv(600E-9), wb_wv(525E-9), wb_wv(460E-9)});
				  lak::vec3f_t aero_wb = wb(aero_match);

				  lrdimg[{x, y}] = rye_to_srgb(irrgb * aero_wb * ir_wb);
			  }
		  });
	}
}

void process_image_async(int black_level,
                         int white_level,
                         float ir_in_red,
                         float ir_in_green,
                         float colour_temp,
                         lak::vec3f_t aero_match)
{
	image_process = lak::async(process_image,
	                           black_level,
	                           white_level,
	                           ir_in_red,
	                           ir_in_green,
	                           colour_temp,
	                           aero_match);
}

struct main_window : lak::basic_window<main_window>
{
	using super_window = lak::basic_window<main_window>;

	static void open_file(const lak::fs::path &path) { load_binary_async(path); }

	static void save_file(const lak::fs::path &path)
	{
		LAK_UNUSED(path);
		// :TODO: DNG output
	}

	static const lak::fs::path &file_path() { return binary_path; }

	static lak::span<byte_t> file_data() { return lak::span(binary); }

	static lak::graphics_mode graphics_mode() { return ::graphics_mode; }

	static bool update() { return binary_update || raw_update; }

	static void file_menu()
	{
		static lak::path_getter open_pgetter, save_pgetter;
		if (auto res = open_pgetter(); res) open_file(*res);
		if (auto res = save_pgetter(); res) save_file(*res);

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Open...", nullptr, false))
				open_pgetter.open_file(file_path(),
				                       "Raw Image Files{.ARW,.RAF,.NEF,.CR3,.CR2},.*");

			// if (ImGui::MenuItem(
			//       "Save...", nullptr, false, processedimg.contig_size() != 0U))
			// 	save_pgetter.save_file(file_path().parent_path() /
			// 	                         (file_path().stem().u8string() + u8".DNG"),
			// 	                       "Image Files{.DNG},.*");

			ImGui::EndMenu();
		}
	}

	static void menu_bar(float) { file_menu(); }

	static void main_region(float frame_time)
	{
		static int lraw_black_level = 0;
		static int lraw_white_level = UINT16_MAX;

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

				lraw_black_level = lraw->imgdata.color.black;
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
			static float ir_in_red     = 1.0f;
			static float ir_in_green   = 1.0f;
			static lak::vec3f_t aero_match{.35f, 1.4f, .7f};
			static float lightness  = 0.f;
			static float contrast   = 0.f;
			static float saturation = 0.f;

			if (image_process && image_process->has_value())
			{
				image_process.reset();
				lrawtex      = rye_create_texture(lrawimg, graphics_mode());
				lrdebayertex = rye_create_texture(lrdimg, graphics_mode());
			}

			if (raw_update && !image_process)
			{
				raw_update = false;
				process_image_async(lraw_black_level,
				                    lraw_white_level,
				                    ir_in_red,
				                    ir_in_green,
				                    colour_temp,
				                    aero_match);
			}

			{
				const auto content_size{ImGui::GetContentRegionAvail()};

				static float left_size  = content_size.x / 2;
				static float right_size = content_size.x / 2;

				lak::VertSplitter(left_size, right_size, content_size.x);

				ImGui::BeginChild(
				  "ImgLeft", {left_size, -1}, true, ImGuiWindowFlags_NoSavedSettings);
				ImGui::Text("Make %s", lraw->imgdata.idata.make);
				ImGui::Text("Model %s", lraw->imgdata.idata.model);
				ImGui::Text("ISO %.0f 1/%.0fs f/%.0f %.0fmm",
				            lraw->imgdata.other.iso_speed,
				            1.0f / lraw->imgdata.other.shutter,
				            lraw->imgdata.other.aperture,
				            lraw->imgdata.other.focal_len);

				ImGui::SliderInt(
				  "Black level", &lraw_black_level, 0, lraw->imgdata.color.maximum);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderInt(
				  "White level", &lraw_white_level, 0, lraw->imgdata.color.maximum);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat("IR in Red", &ir_in_red, 0.1f, 10.0f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat("IR in Green", &ir_in_green, 0.1f, 10.0f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::Separator();

				ImGui::SliderFloat("Temperature", &colour_temp, 2000.f, 10000.f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::SliderFloat3("Match", &aero_match.r, 0.0f, 2.0f);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				ImGui::Separator();

				// ImGui::SliderFloat("Lightness", &lightness, -100.f, 100.f);
				// if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				// ImGui::SliderFloat("Contrast", &contrast, -100.f, 100.f);
				// if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

				// ImGui::SliderFloat("Saturation", &saturation, -100.f, 100.f);
				// if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

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
				// LAK_TREE_NODE("DEBAYER")
				{
					static float lraw_size = 1.0f;
					rye_image_view(lrdebayertex, &lraw_size);
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
}
