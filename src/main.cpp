#include "main.hpp"
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
lak::optional<lak::future<void>> binary_load, image_process;
lak::array<byte_t> binary;
bool binary_update = false, raw_update = false;

int last_white_level = -1;
lak::ImUniqueTexture lrawtex, lrdebayertex, lrprocessedtex, lrsrgbtex,
  lrwavetex, lrwave2tex;

void reset_textures()
{
	lrawtex.reset();
	lrprocessedtex.reset();
	lrdebayertex.reset();
	lrsrgbtex.reset();
	lrwavetex.reset();
	lrwave2tex.reset();
}

lak::optional<LibRaw> lraw;
lak::image<lak::vec3f_t> lrawimg, lrdimg, lrpimg, lrsrgbimg, lrwaveimg,
  lrwave2img;
uint16_t out_colour_temp;

lak::array<lak::vec3f_t> _ir_histo, ir_histo, _white_histo, white_histo,
  _srgb_histo, srgb_histo;

enum struct sensor_format_t
{
	bayer,
	xtrans,
	foveon,
};

sensor_format_t get_sensor_format(LibRaw &lr)
{
	if (lr.imgdata.idata.is_foveon)
		return sensor_format_t::foveon;
	else if (lr.imgdata.idata.filters == 9U)
		return sensor_format_t::xtrans;
	// else if (lr.imgdata.idata.maker_index == LIBRAW_CAMERAMAKER_Minolta &&
	//          lak::astring_view(lr.imgdata.idata.model) == "RD175"_view)
	// 	return sensor_format_t::rd175;
	else
		return sensor_format_t::bayer;
}

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

lak::error_codes<std::error_code, LibRaw_errors> load_binary_ex(
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
	lrawimg.resize({0, 0});
	lrdimg.resize({0, 0});
	lrsrgbimg.resize({0, 0});
	lrwaveimg.resize({0, 0});
	lrwave2img.resize({0, 0});
	binary_load = lak::async(load_binary, path);
}

void process_image_white_level(lak::tasks &tasks,
                               lak::image<lak::vec3f_t> &img,
                               int white_level)
{
	last_white_level = white_level;

	auto format = get_sensor_format(*lraw);

	white_level = static_cast<int>(std::max<unsigned int>(
	  0,
	  std::min<unsigned int>(lraw->imgdata.color.maximum,
	                         static_cast<unsigned int>(white_level))));

	if (format == sensor_format_t::foveon) white_level = (1U << 14U) - 1U;

	img.resize({lraw->imgdata.sizes.iwidth, lraw->imgdata.sizes.iheight});

	// convert raw to float and apply white level adjustment
	for (size_t y = 0; y < img.size().y; ++y)
	{
		tasks.push(
		  [&, iy = y * img.size().x]()
		  {
			  for (size_t x = 0; x < img.size().x; ++x)
			  {
				  const size_t i = iy + x;

				  img[i].r = float(lraw->imgdata.image[i][0]) / white_level;
				  img[i].g = std::max(float(lraw->imgdata.image[i][1]),
				                      float(lraw->imgdata.image[i][3])) /
				             white_level;
				  img[i].b = float(lraw->imgdata.image[i][2]) / white_level;
			  }
		  });
	}
	tasks.await();
}

void process_image_demosaic(lak::tasks &tasks,
                            const lak::image<lak::vec3f_t> &src,
                            lak::image<lak::vec3f_t> &dst)
{
	auto format = get_sensor_format(*lraw);

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

	lak::vec2s_t isize = src.size();

	// downscale demosaic
	if (format == sensor_format_t::foveon)
	{
		dst.resize(flip4(isize));

		for (size_t y = 0; y < isize.y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < isize.x; ++x)
				  {
					  const lak::vec2s_t xy_dst = flip124({x, y}, dst.size());
					  const lak::vec2s_t xy_src{x, y};
					  dst[xy_dst] = src[xy_src];
				  }
			  });
		}

		tasks.await();
	}
	else if (format == sensor_format_t::xtrans)
	{
		isize.x -= (3U * 2U);
		isize.y -= (3U * 2U);
		dst.resize(flip4(isize));

		for (size_t y = 0; y < isize.y / 3U; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  const size_t y3 = y * 3;
				  for (size_t x = 0; x < isize.x / 3U; ++x)
				  {
					  const size_t x3 = x * 3;

					  const lak::vec2s_t xy_dst_00 =
					    flip124({x3 + 0U, y3 + 0U}, dst.size());
					  const lak::vec2s_t xy_dst_01 =
					    flip124({x3 + 0U, y3 + 1U}, dst.size());
					  const lak::vec2s_t xy_dst_02 =
					    flip124({x3 + 0U, y3 + 2U}, dst.size());

					  const lak::vec2s_t xy_dst_10 =
					    flip124({x3 + 1U, y3 + 0U}, dst.size());
					  const lak::vec2s_t xy_dst_11 =
					    flip124({x3 + 1U, y3 + 1U}, dst.size());
					  const lak::vec2s_t xy_dst_12 =
					    flip124({x3 + 1U, y3 + 2U}, dst.size());

					  const lak::vec2s_t xy_dst_20 =
					    flip124({x3 + 2U, y3 + 0U}, dst.size());
					  const lak::vec2s_t xy_dst_21 =
					    flip124({x3 + 2U, y3 + 1U}, dst.size());
					  const lak::vec2s_t xy_dst_22 =
					    flip124({x3 + 2U, y3 + 2U}, dst.size());

					  const lak::vec2s_t xy_src_01_20{x3 + 0U + 2U, y3 + 3U + 0U};
					  const lak::vec2s_t xy_src_01_21{x3 + 0U + 2U, y3 + 3U + 1U};
					  const lak::vec2s_t xy_src_01_22{x3 + 0U + 2U, y3 + 3U + 2U};

					  const lak::vec2s_t xy_src_02_20{x3 + 0U + 2U, y3 + 6U + 0U};

					  const lak::vec2s_t xy_src_10_02{x3 + 3U + 0U, y3 + 0U + 2U};
					  const lak::vec2s_t xy_src_10_12{x3 + 3U + 1U, y3 + 0U + 2U};
					  const lak::vec2s_t xy_src_10_22{x3 + 3U + 2U, y3 + 0U + 2U};

					  const lak::vec2s_t xy_src_11_00{x3 + 3U + 0U, y3 + 3U + 0U};
					  const lak::vec2s_t xy_src_11_01{x3 + 3U + 0U, y3 + 3U + 1U};
					  const lak::vec2s_t xy_src_11_02{x3 + 3U + 0U, y3 + 3U + 2U};
					  const lak::vec2s_t xy_src_11_10{x3 + 3U + 1U, y3 + 3U + 0U};
					  const lak::vec2s_t xy_src_11_11{x3 + 3U + 1U, y3 + 3U + 1U};
					  const lak::vec2s_t xy_src_11_12{x3 + 3U + 1U, y3 + 3U + 2U};
					  const lak::vec2s_t xy_src_11_20{x3 + 3U + 2U, y3 + 3U + 0U};
					  const lak::vec2s_t xy_src_11_21{x3 + 3U + 2U, y3 + 3U + 1U};
					  const lak::vec2s_t xy_src_11_22{x3 + 3U + 2U, y3 + 3U + 2U};

					  const lak::vec2s_t xy_src_12_00{x3 + 3U + 0U, y3 + 6U + 0U};
					  const lak::vec2s_t xy_src_12_10{x3 + 3U + 1U, y3 + 6U + 0U};
					  const lak::vec2s_t xy_src_12_20{x3 + 3U + 2U, y3 + 6U + 0U};

					  const lak::vec2s_t xy_src_20_02{x3 + 6U + 0U, y3 + 0U + 2U};

					  const lak::vec2s_t xy_src_21_00{x3 + 6U + 0U, y3 + 3U + 0U};
					  const lak::vec2s_t xy_src_21_01{x3 + 6U + 0U, y3 + 3U + 1U};
					  const lak::vec2s_t xy_src_21_02{x3 + 6U + 0U, y3 + 3U + 2U};

					  /*
					  G G B  G G R  G G B  G G R
					  G G R  G G B  G G R  G G B
					  R B G  B R G  R B G  B R G

					  G G R  G G B  G G R  G G B
					  G G B  G G R  G G B  G G R
					  B R G  R B G  B R G  R B G

					  G G B  G G R  G G B  G G R
					  G G R  G G B  G G R  G G B
					  R B G  B R G  R B G  B R G

					  G G R  G G B  G G R  G G B
					  G G B  G G R  G G B  G G R
					  B R G  R B G  B R G  R B G
					  */

					  /*
					  G G .  G G .  G G .
					  G G .  G G .  G G .
					  . . G  . . G  . . G

					  G G .  G G .  G G .
					  G G .  G G .  G G .
					  . . G  . . G  . . G

					  G G .  G G .  G G .
					  G G .  G G .  G G .
					  . . G  . . G  . . G
					  */

					  dst[xy_dst_00].g = src[xy_src_11_00].g;
					  dst[xy_dst_10].g = src[xy_src_11_10].g;
					  dst[xy_dst_20].g = (src[xy_src_10_22].g + src[xy_src_11_10].g +
					                      src[xy_src_21_00].g) /
					                     3.f;

					  dst[xy_dst_01].g = src[xy_src_11_01].g;
					  dst[xy_dst_11].g = src[xy_src_11_11].g;
					  dst[xy_dst_21].g = (src[xy_src_11_22].g + src[xy_src_11_11].g +
					                      src[xy_src_21_01].g) /
					                     3.f;

					  dst[xy_dst_02].g = (src[xy_src_01_22].g + src[xy_src_11_01].g +
					                      src[xy_src_12_00].g) /
					                     3.f;
					  dst[xy_dst_12].g = (src[xy_src_11_22].g + src[xy_src_11_11].g +
					                      src[xy_src_12_10].g) /
					                     3.f;
					  dst[xy_dst_22].g = src[xy_src_11_22].g;

					  constexpr float nb4_4 = 0.05f;
					  constexpr float nb4_3 = 0.1f;
					  constexpr float nb4_2 = 0.2f;
					  constexpr float nb4_1 = 1.f - (nb4_2 + nb4_3 + nb4_4);

					  constexpr float nb3_2 = 0.2f;
					  constexpr float nb3_1 = 1.f - (nb3_2 + nb3_2);

					  if (((x + y) % 2U) == 0U)
					  {
						  /*
						  . . .  . . B  . . .
						  . . B  . . .  . . B
						  B . .  . B .  B . .

						  . . B  . . .  . . B
						  . . .  . . B  . . .
						  . B .  B . .  . B .

						  . . .  . . B  . . .
						  . . B  . . .  . . B
						  B . .  . B .  B . .
						  */
						  dst[xy_dst_00].b =
						    (nb4_1 * src[xy_src_01_20].b) + (nb4_2 * src[xy_src_10_12].b) +
						    (nb4_3 * src[xy_src_11_02].b) + (nb4_4 * src[xy_src_11_21].b);
						  dst[xy_dst_10].b =
						    (nb4_1 * src[xy_src_10_12].b) + (nb4_2 * src[xy_src_11_21].b) +
						    (nb4_3 * src[xy_src_01_20].b) + (nb4_4 * src[xy_src_11_02].b);
						  dst[xy_dst_20].b = (nb3_1 * src[xy_src_11_21].b) +
						                     (nb3_2 * src[xy_src_10_12].b) +
						                     (nb3_2 * src[xy_src_20_02].b);

						  dst[xy_dst_01].b =
						    (nb4_1 * src[xy_src_11_02].b) + (nb4_2 * src[xy_src_01_20].b) +
						    (nb4_3 * src[xy_src_11_21].b) + (nb4_4 * src[xy_src_10_12].b);
						  dst[xy_dst_11].b =
						    (nb4_1 * src[xy_src_11_21].b) + (nb4_2 * src[xy_src_11_02].b) +
						    (nb4_3 * src[xy_src_10_12].b) + (nb4_4 * src[xy_src_01_20].b);
						  dst[xy_dst_21].b = src[xy_src_11_21].b;

						  dst[xy_dst_02].b = src[xy_src_11_02].b;
						  dst[xy_dst_12].b = (nb3_1 * src[xy_src_11_02].b) +
						                     (nb3_2 * src[xy_src_11_21].b) +
						                     (nb3_2 * src[xy_src_12_20].b);
						  dst[xy_dst_22].b =
						    (src[xy_src_11_21].b + src[xy_src_12_20].b) / 2.f;

						  /*
						  . . R  . . .  . . R
						  . . .  . . R  . . .
						  . R .  R . .  . R .

						  . . .  . . R  . . .
						  . . R  . . .  . . R
						  R . .  . R .  R . .

						  . . R  . . .  . . R
						  . . .  . . R  . . .
						  . R .  R . .  . R .
						  */
						  dst[xy_dst_00].r =
						    (nb4_1 * src[xy_src_10_02].r) + (nb4_2 * src[xy_src_01_21].r) +
						    (nb4_3 * src[xy_src_11_20].r) + (nb4_4 * src[xy_src_11_12].r);
						  dst[xy_dst_10].r =
						    (nb4_1 * src[xy_src_11_20].r) + (nb4_2 * src[xy_src_10_02].r) +
						    (nb4_3 * src[xy_src_11_12].r) + (nb4_4 * src[xy_src_01_21].r);
						  dst[xy_dst_20].r = src[xy_src_11_20].r;

						  dst[xy_dst_01].r =
						    (nb4_1 * src[xy_src_01_21].r) + (nb4_2 * src[xy_src_11_12].r) +
						    (nb4_3 * src[xy_src_10_02].r) + (nb4_4 * src[xy_src_11_20].r);
						  dst[xy_dst_11].r =
						    (nb4_1 * src[xy_src_11_12].r) + (nb4_2 * src[xy_src_11_20].r) +
						    (nb4_3 * src[xy_src_01_21].r) + (nb4_4 * src[xy_src_10_02].r);
						  dst[xy_dst_21].r = (nb3_1 * src[xy_src_11_20].r) +
						                     (nb3_2 * src[xy_src_11_12].r) +
						                     (nb3_2 * src[xy_src_21_02].r);

						  dst[xy_dst_02].r = (nb3_1 * src[xy_src_11_12].r) +
						                     (nb3_2 * src[xy_src_01_21].r) +
						                     (nb3_2 * src[xy_src_02_20].r);
						  dst[xy_dst_12].r = src[xy_src_11_12].r;
						  dst[xy_dst_22].r =
						    (src[xy_src_11_12].r + src[xy_src_21_02].r) / 2.f;
					  }
					  else
					  {
						  /*
						  . . B  . . .  . . B
						  . . .  . . B  . . .
						  . B .  B . .  . B .

						  . . .  . . B  . . .
						  . . B  . . .  . . B
						  B . .  . B .  B . .

						  . . B  . . .  . . B
						  . . .  . . B  . . .
						  . B .  B . .  . B .
						  */
						  dst[xy_dst_00].b =
						    (nb4_1 * src[xy_src_10_02].b) + (nb4_2 * src[xy_src_01_21].b) +
						    (nb4_3 * src[xy_src_11_20].b) + (nb4_4 * src[xy_src_11_12].b);
						  dst[xy_dst_10].b =
						    (nb4_1 * src[xy_src_11_20].b) + (nb4_2 * src[xy_src_10_02].b) +
						    (nb4_3 * src[xy_src_11_12].b) + (nb4_4 * src[xy_src_01_21].b);
						  dst[xy_dst_20].b = src[xy_src_11_20].b;

						  dst[xy_dst_01].b =
						    (nb4_1 * src[xy_src_01_21].b) + (nb4_2 * src[xy_src_11_12].b) +
						    (nb4_3 * src[xy_src_10_02].b) + (nb4_4 * src[xy_src_11_20].b);
						  dst[xy_dst_11].b =
						    (nb4_1 * src[xy_src_11_12].b) + (nb4_2 * src[xy_src_11_20].b) +
						    (nb4_3 * src[xy_src_01_21].b) + (nb4_4 * src[xy_src_10_02].b);
						  dst[xy_dst_21].b = (nb3_1 * src[xy_src_11_20].b) +
						                     (nb3_2 * src[xy_src_11_12].b) +
						                     (nb3_2 * src[xy_src_21_02].b);

						  dst[xy_dst_02].b = (nb3_1 * src[xy_src_11_12].b) +
						                     (nb3_2 * src[xy_src_01_21].b) +
						                     (nb3_2 * src[xy_src_02_20].b);
						  dst[xy_dst_12].b = src[xy_src_11_12].b;
						  dst[xy_dst_22].b =
						    (src[xy_src_11_12].b + src[xy_src_21_02].b) / 2.f;

						  /*
						  . . .  . . R  . . .
						  . . R  . . .  . . R
						  R . .  . R .  R . .

						  . . R  . . .  . . R
						  . . .  . . R  . . .
						  . R .  R . .  . R .

						  . . .  . . R  . . .
						  . . R  . . .  . . R
						  R . .  . R .  R . .
						  */
						  dst[xy_dst_00].r =
						    (nb4_1 * src[xy_src_01_20].r) + (nb4_2 * src[xy_src_10_12].r) +
						    (nb4_3 * src[xy_src_11_02].r) + (nb4_4 * src[xy_src_11_21].r);
						  dst[xy_dst_10].r =
						    (nb4_1 * src[xy_src_10_12].r) + (nb4_2 * src[xy_src_11_21].r) +
						    (nb4_3 * src[xy_src_01_20].r) + (nb4_4 * src[xy_src_11_02].r);
						  dst[xy_dst_20].r = (nb3_1 * src[xy_src_11_21].r) +
						                     (nb3_2 * src[xy_src_10_12].r) +
						                     (nb3_2 * src[xy_src_20_02].r);

						  dst[xy_dst_01].r =
						    (nb4_1 * src[xy_src_11_02].r) + (nb4_2 * src[xy_src_01_20].r) +
						    (nb4_3 * src[xy_src_11_21].r) + (nb4_4 * src[xy_src_10_12].r);
						  dst[xy_dst_11].r =
						    (nb4_1 * src[xy_src_11_21].r) + (nb4_2 * src[xy_src_11_02].r) +
						    (nb4_3 * src[xy_src_10_12].r) + (nb4_4 * src[xy_src_01_20].r);
						  dst[xy_dst_21].r = src[xy_src_11_21].r;

						  dst[xy_dst_02].r = src[xy_src_11_02].r;
						  dst[xy_dst_12].r = (nb3_1 * src[xy_src_11_02].r) +
						                     (nb3_2 * src[xy_src_11_21].r) +
						                     (nb3_2 * src[xy_src_12_20].r);
						  dst[xy_dst_22].r =
						    (src[xy_src_11_21].r + src[xy_src_12_20].r) / 2.f;
					  }
				  }
			  });
		}

		tasks.await();
	}
	else
	{
		isize.x -= (2U * 2U);
		isize.y -= (2U * 2U);
		dst.resize(flip4(isize));

		lak::vec2s_t channels[4U] = {{0U, 0U}, {0U, 0U}, {0U, 0U}, {0U, 0U}};
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
				if (int col = lraw->COLOR(y, x); col <= 3)
					channels[size_t(col)] = {size_t(x), size_t(y)};

		for (size_t y = 0; y < isize.y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < isize.x; ++x)
				  {
					  const lak::vec2s_t xy_dst = flip124({x, y}, dst.size());
					  const lak::vec2s_t xy_src{x + 2U, y + 2U};

					  /*
					  R G R G
					  G B G B
					  R G R G
					  G B G B
					  */

					  /*
					  R . R .
					  . . . .
					  R . R .
					  . . . .
					  */

					  if (bool r_x = ((x % 2U) == channels[0U].x),
					      r_y      = ((y % 2U) == channels[0U].y);
					      r_x && r_y) [[unlikely]]
					  {
						  dst[xy_dst].r = src[xy_src].r;
					  }
					  else if (!(r_x || r_y))
					  {
						  dst[xy_dst].r = (src[{xy_src.x - 1U, xy_src.y - 1U}].r +
						                   src[{xy_src.x - 1U, xy_src.y + 1U}].r +
						                   src[{xy_src.x + 1U, xy_src.y - 1U}].r +
						                   src[{xy_src.x + 1U, xy_src.y + 1U}].r) /
						                  4.f;
					  }
					  else if (r_x)
					  {
						  dst[xy_dst].r = (src[{xy_src.x, xy_src.y - 1U}].r +
						                   src[{xy_src.x, xy_src.y + 1U}].r) /
						                  2.f;
					  }
					  else /* if (r_y) */
					  {
						  dst[xy_dst].r = (src[{xy_src.x - 1U, xy_src.y}].r +
						                   src[{xy_src.x + 1U, xy_src.y}].r) /
						                  2.f;
					  }

					  /*
					  . . . .
					  . B . B
					  . . . .
					  . B . B
					  */

					  if (bool b_x = ((x % 2U) == channels[2U].x),
					      b_y      = ((y % 2U) == channels[2U].y);
					      b_x && b_y) [[unlikely]]
					  {
						  dst[xy_dst].b = src[xy_src].b;
					  }
					  else if (!(b_x || b_y))
					  {
						  dst[xy_dst].b = (src[{xy_src.x - 1U, xy_src.y - 1U}].b +
						                   src[{xy_src.x - 1U, xy_src.y + 1U}].b +
						                   src[{xy_src.x + 1U, xy_src.y - 1U}].b +
						                   src[{xy_src.x + 1U, xy_src.y + 1U}].b) /
						                  4.f;
					  }
					  else if (b_x)
					  {
						  dst[xy_dst].b = (src[{xy_src.x, xy_src.y - 1U}].b +
						                   src[{xy_src.x, xy_src.y + 1U}].b) /
						                  2.f;
					  }
					  else /* if (b_y) */
					  {
						  dst[xy_dst].b = (src[{xy_src.x - 1U, xy_src.y}].b +
						                   src[{xy_src.x + 1U, xy_src.y}].b) /
						                  2.f;
					  }

					  /*
					  . G . G
					  G . G .
					  . G . G
					  G . G .
					  */

					  if (lak::vec2s_t xy_m2{xy_src.x % 2U, xy_src.y % 2U};
					      (xy_m2 == channels[1U]) || (xy_m2 == channels[3U]))
					  {
						  dst[xy_dst].g = src[xy_src].g;
					  }
					  else
					  {
						  dst[xy_dst].g = (src[{xy_src.x - 1U, xy_src.y}].g +
						                   src[{xy_src.x + 1U, xy_src.y}].g +
						                   src[{xy_src.x, xy_src.y - 1U}].g +
						                   src[{xy_src.x, xy_src.y + 1U}].g) /
						                  4.f;
					  }
				  }
			  });
		}

		tasks.await();
	}
}

void process_image_ir_stage_1(lak::tasks &tasks,
                              lak::image<lak::vec3f_t> &img,
                              lak::vec3f_t ir_in)
{
	auto format = get_sensor_format(*lraw);

	if (format == sensor_format_t::foveon)
	{
		const lak::mat3f_t ir_channel_swap{lak::vec3{
		  lak::vec3f_t{0.f, 0.f, 1.f / ir_in.b},
		  lak::vec3f_t{1.f, 0.f, -ir_in.r / ir_in.b},
		  lak::vec3f_t{0.f, 1.f, -ir_in.g / ir_in.b},
		}};

		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  img[{x, y}] *= ir_channel_swap;
				  }
			  });
		}

		tasks.await();
	}
	else
	{
		const float ir_in_red   = ir_in.r / ir_in.b;
		const float ir_in_green = ir_in.g / ir_in.b;

		const lak::mat3f_t ir_channel_swap{lak::vec3{
		  lak::vec3f_t{0.f, 0.f, 1.f},
		  lak::vec3f_t{1.f, 0.f, -ir_in.r / ir_in.b},
		  lak::vec3f_t{0.f, 1.f, -ir_in.g / ir_in.b},
		}};

		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  img[{x, y}] *= ir_channel_swap;
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

	auto wb_wv      = rye_relative_blackbody(colour_temp);
	out_colour_temp = uint16_t(std::max<long long>(
	  0, std::min<long long>((1U << 15U) - 1, std::llround(colour_temp))));

	// camera sensitivity compensation
	const lak::vec3f_t aero_match_balance = rye_white_balance(
	  {1.f / aero_match.r, 1.f / aero_match.g, 1.f / aero_match.b});

	// blackbody whitebalance
	const lak::vec3f_t temp_sensitivity{
	  wb_wv(850.0), wb_wv(600.0), wb_wv(525.0)};
	const lak::vec3f_t temp_balance = rye_white_balance(temp_sensitivity);

	// aerochrome sensitivity factor
	const lak::vec3f_t aerochrome_sensitivity{
	  std::exp(0.5f), std::exp(1.5f), std::exp(1.4f)};
	const lak::vec3f_t aero_balance = rye_white_balance(aerochrome_sensitivity);

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

				  const float min = rye_vec_min<float>(irrgb);
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

	bool is_foveon                  = lraw->imgdata.idata.is_foveon;
	[[maybe_unused]] bool is_xtrans = lraw->imgdata.idata.filters == 9U;

	if (lrawimg.contig_size() == 0 || white_level != last_white_level)
	{
		process_image_white_level(tasks, lrawimg, white_level);
		lrdimg.resize({0, 0});
	}

	// if (lrdimg.contig_size() == 0)
	{
		process_image_demosaic(tasks, lrawimg, lrdimg);
	}

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
					  p *= lak::vec3f_t{
					    lraw->imgdata.color.pre_mul[0],
					    lraw->imgdata.color.pre_mul[1],
					    lraw->imgdata.color.pre_mul[2],
					  };

					  lak::mat3f_t rgb_cam{lak::vec3{
					    lak::vec3f_t{lraw->imgdata.color.rgb_cam[0][0],
					                 lraw->imgdata.color.rgb_cam[0][1],
					                 lraw->imgdata.color.rgb_cam[0][2]},
					    lak::vec3f_t{lraw->imgdata.color.rgb_cam[1][0],
					                 lraw->imgdata.color.rgb_cam[1][1],
					                 lraw->imgdata.color.rgb_cam[1][2]},
					    lak::vec3f_t{lraw->imgdata.color.rgb_cam[2][0],
					                 lraw->imgdata.color.rgb_cam[2][1],
					                 lraw->imgdata.color.rgb_cam[2][2]},
					  }};

					  p *= rgb_cam;
				  }

				  p = rye_exp_correction(p, exposure, lightness, contrast, saturation);
				  p = rye_to_srgb(p);
				  return p;
			  };
			  if (desqueeze)
			  {
				  auto sampler = rye_desqueeze_sampler(lrdimg, lrpimg.size());
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

		lrwaveimg = rye_waveform(wavetemp);
		_ir_histo = rye_histogram(wavetemp);
	}

	process_image_ir_stage_1(tasks, lrdimg, ir_balance.ir_in);

	process_image_ir_stage_2(tasks, lrdimg, ir_balance.aero_match, colour_temp);

	// generate white balance histogram and waveform
	lrwave2img   = rye_waveform(lrdimg);
	_white_histo = rye_histogram(lrdimg);

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
				  auto sampler = rye_desqueeze_sampler(lrdimg, lrsrgbimg.size());
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrsrgbimg.size().x; ++xy.x)
				  {
					  lrsrgbimg[xy] = sampler(xy);
					  lrsrgbimg[xy] = rye_exp_correction(
					    lrsrgbimg[xy], exposure, lightness, contrast, saturation);
					  lrsrgbimg[xy] = rye_to_srgb(lrsrgbimg[xy]);
				  }
			  }
			  else
			  {
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrsrgbimg.size().x; ++xy.x)
				  {
					  lrsrgbimg[xy] = lrdimg[xy];
					  lrsrgbimg[xy] = rye_exp_correction(
					    lrsrgbimg[xy], exposure, lightness, contrast, saturation);
					  lrsrgbimg[xy] = rye_to_srgb(lrsrgbimg[xy]);
				  }
			  }
		  });
	}
	tasks.await();

	// generate final histogram
	_srgb_histo = rye_histogram(lrsrgbimg);
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

struct main_window : lak::basic_window<main_window>
{
	using super_window = lak::basic_window<main_window>;

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
				img16[xy].r = static_cast<uint16_t>(std::max<long long>(
				  std::min<long long>(std::llround(lrdimg[xy].r * float(UINT16_MAX)),
				                      UINT16_MAX),
				  0));
				img16[xy].g = static_cast<uint16_t>(std::max<long long>(
				  std::min<long long>(std::llround(lrdimg[xy].g * float(UINT16_MAX)),
				                      UINT16_MAX),
				  0));
				img16[xy].b = static_cast<uint16_t>(std::max<long long>(
				  std::min<long long>(std::llround(lrdimg[xy].b * float(UINT16_MAX)),
				                      UINT16_MAX),
				  0));
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
		ifd0.push_Make(lak::astring_view((const char *)lraw->imgdata.idata.make));
		ifd0.push_Model(
		  lak::astring_view((const char *)lraw->imgdata.idata.model));
		ifd0.push_Orientation(lak::fixed_array((uint16_t(1U))));
		ifd0.push_SamplesPerPixel(lak::fixed_array(uint16_t(3U)));

		ifd0.push_XResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_YResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_ResolutionUnit(lak::fixed_array(uint16_t(2U)));
		ifd0.push_Software(APP_NAME ""_view);
		ifd0.push_SampleFormat({1U});

		if (lraw->imgdata.other.shutter != 0.f)
			ifd0.push_ExposureTime(lak::fixed_array(lak::tiff::urational{
			  1000U, uint32_t((1.f / lraw->imgdata.other.shutter) * 1000)}));
		ifd0.push_FNumber(lak::fixed_array(lak::tiff::urational{
		  uint32_t(lraw->imgdata.other.aperture * 100), 100U}));
		ifd0.push_ISOSpeedRatings(
		  lak::fixed_array(uint16_t(lraw->imgdata.other.iso_speed)));
		ifd0.push_FocalLength(lak::fixed_array(lak::tiff::urational{
		  uint32_t(lraw->imgdata.other.focal_len * 100), 100U}));

		ifd0.push_DNGVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_DNGBackwardVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_UniqueCameraModel(lak::string_view(
		  lraw->imgdata.idata.make + " "_str + lraw->imgdata.idata.model));
		ifd0.push_CameraSerialNumber(
		  lak::astring_view((const char *)lraw->imgdata.shootinginfo.BodySerial));

		auto &exif = ifd0.push_exif();

		exif.push_LensMake(
		  lak::astring_view((const char *)lraw->imgdata.lens.LensMake));
		exif.push_LensModel(
		  lak::astring_view((const char *)lraw->imgdata.lens.Lens));
		exif.push_LensSerialNumber(
		  lak::astring_view((const char *)lraw->imgdata.lens.LensSerial));

		strm.write<lak::endian::native>(tiff).UNWRAP();

		lak::save_file(path, strm.data);
	}

	const lak::fs::path &file_path() { return binary_path; }

	lak::span<byte_t> file_data() { return lak::span(binary); }

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

	void menu_bar(float frame_time)
	{
		file_menu();
		about_menu(frame_time);
		ImGui::Checkbox("Use sensor database IR white point",
		                &use_database_ir_balance);
		ImGui::Checkbox("Use sensor database RGB white point",
		                &use_database_aero_match);
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
		if (binary_load)
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

			if (binary_load->has_value())
			{
				binary_load.reset();
				binary_update = true;
				raw_update    = true;
				time_acc      = 0.0f;

				reset_textures();

				ir_histo.clear();
				white_histo.clear();
				srgb_histo.clear();

				lraw_white_level = lraw->imgdata.color.maximum;

				used_ir_balance_from_db = false;
				used_aero_match_from_db = false;
				if (use_database_ir_balance || use_database_aero_match)
				{
					if (auto it = ir_balance_db.find(lraw->imgdata.idata.make + " "_str +
					                                 lraw->imgdata.idata.model);
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
			if (image_process && image_process->has_value())
			{
				image_process.reset();
				ir_histo    = lak::move(_ir_histo);
				white_histo = lak::move(_white_histo);
				srgb_histo  = lak::move(_srgb_histo);
				lrawtex.emplace(lrawimg);
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

				ImGui::BeginChild(
				  "ImgLeft", {left_size, -1}, true, ImGuiWindowFlags_NoSavedSettings);
				ImGui::Text("%s %s + %s",
				            lraw->imgdata.idata.make,
				            lraw->imgdata.idata.model,
				            lraw->imgdata.lens.Lens);
				ImGui::Text("ISO %.0f 1/%.0fs f/%.0f %.0fmm",
				            lraw->imgdata.other.iso_speed,
				            1.0f / lraw->imgdata.other.shutter,
				            lraw->imgdata.other.aperture,
				            lraw->imgdata.other.focal_len);

				ImGui::Separator();

				ImGui::Text("Camera Settings");

				ImGui::SliderInt(
				  "White level", &lraw_white_level, 0, lraw->imgdata.color.maximum);
				if (ImGui::IsItemDeactivatedAfterEdit()) raw_update = true;

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
				LAK_TREE_NODE("RAW") { rye_image_view(lrawtex, &lrawtex_size); }
				LAK_TREE_NODE("PROC RAW")
				{
					rye_image_view(lrprocessedtex, &lrawptex_size);
				}
				LAK_TREE_NODE("IR BALANCE WAVEFORM")
				{
					rye_image_view(lrwavetex, &lrawwtex_size);
				}
				LAK_TREE_NODE("WHITE BALANCE WAVEFORM")
				{
					rye_image_view(lrwave2tex, &lraww2tex_size);
				}
				LAK_TREE_NODE("DEBAYER")
				{
					rye_image_view(lrdebayertex, &lrawdtex_size);
				}
				// LAK_TREE_NODE("sRGB")
				{
					rye_image_view(lrsrgbtex, &lrawsrgbtex_size);
				}
				ImGui::EndChild();
			}
		}
	}
};

struct rye_window : virtual public basic_window_api
{
	rye_window() : basic_window_api() {}

	const lak::cobalt::graphics_context *gc;

	virtual ~rye_window()
	{
		lraw.reset();
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

	main_window mwnd;

	virtual void loop(uint64_t counter_delta) override final
	{
		const float frame_time =
		  (float)counter_delta / lak::performance_frequency();

		mwnd.draw(frame_time);

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

	lraw.emplace();

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
