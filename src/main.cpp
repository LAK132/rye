#include "rye_git.hpp"
#define APP_VERSION GIT_TAG "-" GIT_HASH
#define APP_NAME    "rye " STRINGIFY(LAK_ARCH) " " APP_VERSION
#define LAK_BASIC_PROGRAM_IMGUI_WINDOW_IMPL

#include <lak/basic_program.inl>
#include <lak/string_literals.hpp>
#include <lak/string_view.hpp>
#include <lak/test.hpp>

#include <lak/imgui/widgets.hpp>

lak::optional<int> basic_window_preinit(int argc, char **argv)
{
	if (argc == 2 && argv[1] == lak::astring("--version"))
	{
		std::cout << APP_NAME "\n";
		return lak::optional<int>(0);
	}

	lak::debugger.std_out(u8"", u8"" APP_NAME "\n");

	for (int arg = 1; arg < argc; ++arg)
	{
		const auto v{lak::astring_view::from_c_str(argv[arg])};

		if (v == "-h"_view || v == "--help"_view)
		{
			std::cout << "rye [--help]\n";

			return lak::optional<int>(0);
		}
		else if (v == "--nogl"_view)
		{
			basic_window_force_software = true;
		}
		else if (v == "--listtests"_view)
		{
			lak::debugger.std_out(lak::u8string(),
			                      lak::u8string(u8"Available tests:\n"));
			for (const auto &[name, func] : lak::registered_tests())
			{
				lak::debugger.std_out(lak::u8string(),
				                      lak::to_u8string(name) + u8"\n");
			}
			return lak::optional<int>(0);
		}
		else if (v == "--laktestall"_view)
		{
			return lak::optional<int>(lak::run_tests());
		}
		else if (v == "--laktests"_view || v == "--laktest"_view)
		{
			++arg;
			if (arg >= argc) FATAL("Missing tests");
			return lak::optional<int>(lak::run_tests(lak::as_u8string(v)));
		}
	}

	basic_window_target_framerate      = 30;
	basic_window_opengl_settings.major = 3;
	basic_window_opengl_settings.minor = 2;
	// basic_window_clear_colour          = {0.0f, 0.0f, 0.0f, 1.0f};

	return lak::nullopt;
}

void basic_window_init(lak::window &window)
{
	lak::ConfigureFileDialog(window.graphics());
}

void basic_window_handle_event(lak::window &window, lak::event &event)
{
	(void)window;
	(void)event;
}

void basic_window_loop(lak::window &window, uint64_t counter_delta)
{
	(void)window;
	(void)counter_delta;
}

int basic_window_quit(lak::window &window)
{
	(void)window;

	return 0;
}
