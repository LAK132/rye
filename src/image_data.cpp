#include "image_data.hpp"

#include <lak/string_literals/view.hpp>
#include <lak/string_utils.hpp>
#include <lak/string_view.hpp>

lak::future<lak::result<rye::image_data, lak::u8string>> rye::load_image_async(
  const std::filesystem::path &path)
{
	return lak::async(rye::load_image, path);
}

lak::result<rye::image_data, lak::u8string> rye::load_image(
  std::filesystem::path path)
{
	lak::array<lak::u8string> err_msg;

	lak::u8string extension = path.extension().u8string();
	for (auto &c : extension)
		if (c >= u8'a' && c <= u8'z') c = u8'A' + (c - u8'a');

	if (u8".MDC"_view == extension)
	{
		// we handle MDCs far better than libraw likely ever will
		RES_TRY_ASSIGN_ERR(auto err =, rye::load_mdc(path));
		return lak::err_t{lak::fmt<u8"MDC error: {}">(err)};
	}

	// start with trying to use libraw

	{
		RES_TRY_ASSIGN_ERR(auto err =, load_libraw(path));
		err_msg.push_back(lak::fmt<u8"libraw error: {}">(err));
	}

	// fall back for raws we have our own processors for

	if (u8".X3F"_view == extension)
	{
		RES_TRY_ASSIGN_ERR(auto err =, rye::load_x3f(path));
		err_msg.push_back(lak::fmt<u8"X3F error: {}">(err));
	}

	if (err_msg.empty())
		err_msg.push_back(lak::fmt<u8"Failed to find image loader for '{}'">(
		  lak::string_view(path.u8string())));

	return lak::err_t<lak::u8string>{
	  lak::join_strings<char8_t>(u8", "_view, err_msg)};
}
