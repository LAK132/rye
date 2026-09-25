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

	{
		RES_TRY_ASSIGN_ERR(auto err =, load_libraw(path));
		err_msg.push_back(lak::fmt<u8"libraw error: {}">(err));
	}

	if (err_msg.empty())
		err_msg.push_back(lak::fmt<u8"Failed to find image loader for '{}'">(
		  lak::string_view(path.u8string())));

	return lak::err_t<lak::u8string>{
	  lak::join_strings<char8_t>(u8", "_view, err_msg)};
}
