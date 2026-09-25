#include "rye.hpp"

#include <lak/tasks.hpp>

void rye::image_view(ImTextureRef texture, float *scale)
{
	ImGui::DragFloat("Scale", scale, 0.01f, 0.1f, 10.0f);
	ImGui::Separator();
	rye::image_view(texture, *scale);
}

void rye::image_view(ImTextureRef texture, const float scale)
{
	ImGui::BeginChild("Image View",
	                  ImVec2(0, 0),
	                  false,
	                  ImGuiWindowFlags_NoSavedSettings |
	                    ImGuiWindowFlags_AlwaysVerticalScrollbar |
	                    ImGuiWindowFlags_AlwaysHorizontalScrollbar);

	if (texture.GetTexID() == ImTextureID_Invalid)
	{
		ImGui::Text("No image selected.");
	}
	else
	{
		auto sz = lak::TextureSize(texture);
		ImGui::Image(texture, ImVec2(scale * (float)sz.x, scale * (float)sz.y));
	}

	ImGui::EndChild();
}

lak::vec3f_t rye::desqueeze_sample(const lak::image<lak::vec3f_t> &src,
                                   lak::vec2s_t dst_size,
                                   lak::vec2s_t dst_coord)
{
	return rye::desqueeze_sampler(src, dst_size)(dst_coord);
}

lak::image<lak::vec3f_t> rye::desqueeze(lak::tasks &tasks,
                                        const lak::image<lak::vec3f_t> &img,
                                        float desqueeze)
{
	lak::image<lak::vec3f_t> result;

	result.resize(
	  {static_cast<size_t>(std::llround(double(img.size().x) * desqueeze)),
	   img.size().y});

	auto sampler = rye::desqueeze_sampler(img, result.size());

	for (size_t y = 0U; y < result.size().y; ++y)
		tasks.push(
		  [&, y = y, xm = result.size().x]()
		  {
			  for (lak::vec2s_t xy{0U, y}; xy.x < xm; ++xy.x)
				  result[xy] = sampler(xy);
		  });

	tasks.await();

	return result;
}

lak::image<lak::vec3f_t> rye::desqueeze(const lak::image<lak::vec3f_t> &img,
                                        float desqueeze)
{
	lak::tasks tasks{0U};
	return rye::desqueeze(tasks, img, desqueeze);
}

lak::image<lak::vec3f_t> rye::waveform(lak::tasks &tasks,
                                       const lak::image<lak::vec3f_t> &img)
{
	constexpr size_t wave_size = 1024U;
	const float wave_step      = 100.f / float(img.size().y);

	lak::image<lak::vec3f_t> result;
	result.resize({img.size().x, wave_size});
	result.fill({0.f, 0.f, 0.f});

	auto wave_clamp = [](float v) -> size_t
	{
		v = std::log10((v * 90.f) + 10.f) - 1.f;
		if (v <= 0.f)
			return 0U;
		else if (v >= 1.f)
			return wave_size - 1U;

		size_t res = static_cast<size_t>(v * wave_size);
		if (res >= wave_size)
			return wave_size - 1U;
		else
			return res;
	};

	for (size_t x = 0; x < img.size().x; ++x)
	{
		tasks.push(
		  [&, x = x, ym = result.size().y]()
		  {
			  for (size_t y = 0; y < ym; ++y)
			  {
				  const lak::vec3f_t &irgb = img[{x, y}];
				  result[{x, (wave_size - 1U) - wave_clamp(irgb.r)}].r += wave_step;
				  result[{x, (wave_size - 1U) - wave_clamp(irgb.g)}].g += wave_step;
				  result[{x, (wave_size - 1U) - wave_clamp(irgb.b)}].b += wave_step;
			  }
		  });
	}

	tasks.await();

	return result;
}

lak::image<lak::vec3f_t> rye::waveform(const lak::image<lak::vec3f_t> &img)
{
	lak::tasks tasks{0U};
	return rye::waveform(tasks, img);
}

lak::array<lak::vec3f_t> rye::histogram(lak::tasks &tasks,
                                        const lak::image<lak::vec3f_t> &img)
{
	constexpr size_t hist_size = 256U;
	const float hist_step      = 100.f / float(img.contig_size());

	lak::array<lak::vec3f_t> result;
	result.resize(hist_size, lak::vec3f_t{0, 0, 0});

	auto hist_clamp = [](float v) -> size_t
	{
		v = std::log10((v * 90.f) + 10.f) - 1.f;
		if (v <= 0.f)
			return 0U;
		else if (v >= 1.f)
			return hist_size - 1U;

		size_t res = static_cast<size_t>(v * hist_size);
		if (res >= hist_size)
			return hist_size - 1U;
		else
			return res;
	};

	for (size_t y = 0; y < img.size().y; ++y)
	{
		tasks.push(
		  [&, y = y, xm = img.size().x]()
		  {
			  for (size_t x = 0; x < xm; ++x)
			  {
				  const lak::vec3f_t &irgb = img[{x, y}];
				  result[hist_clamp(irgb.r)].r += hist_step;
				  result[hist_clamp(irgb.g)].g += hist_step;
				  result[hist_clamp(irgb.b)].b += hist_step;
			  }
		  });
	}

	tasks.await();

	return result;
}

lak::array<lak::vec3f_t> rye::histogram(const lak::image<lak::vec3f_t> &img)
{
	lak::tasks tasks{0U};
	return rye::histogram(tasks, img);
}

static auto transform_coords(rye::image_flip_t flip)
{
	return [flip](lak::vec2s_t index, lak::vec2s_t size) -> lak::vec2s_t
	{
		if ((flip & rye::image_flip_t::transpose) != rye::image_flip_t::none)
			index = {index.y, index.x};
		if ((flip & rye::image_flip_t::reverse_x) != rye::image_flip_t::none)
			index.x = (size.x - 1U) - index.x;
		if ((flip & rye::image_flip_t::reverse_y) != rye::image_flip_t::none)
			index.y = (size.y - 1U) - index.y;
		return index;
	};
}

lak::image<lak::vec3f_t> rye::transform(lak::tasks &tasks,
                                        const lak::image<lak::vec3f_t> &src,
                                        rye::image_flip_t flip)
{
	auto maybe_transpose = transform_coords(rye::image_flip_t::transpose & flip);
	auto maybe_reverse = transform_coords(rye::image_flip_t::reverse_xy & flip);
	auto maybe_reverse_transpose = transform_coords(flip);

	const lak::vec2s_t isize = src.size();

	lak::image<lak::vec3f_t> dst;
	dst.resize(maybe_transpose(isize, {0U, 0U}));

	for (size_t y = 0; y < isize.y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < isize.x; ++x)
			  {
				  const lak::vec2s_t xy_dst =
				    maybe_reverse_transpose({x, y}, dst.size());
				  const lak::vec2s_t xy_src{x, y};
				  dst[xy_dst] = src[xy_src];
			  }
		  });
	}

	tasks.await();

	return dst;
}

lak::image<lak::vec3f_t> rye::transform(const lak::image<lak::vec3f_t> &src,
                                        rye::image_flip_t flip)
{
	lak::tasks tasks{0U};
	return rye::transform(tasks, src, flip);
}

lak::image<lak::vec3f_t> rye::crop(lak::tasks &tasks,
                                   const lak::image<lak::vec3f_t> &src,
                                   lak::vec2s_t offset,
                                   lak::vec2s_t size)
{
	const lak::vec2s_t isize = src.size();
	if ((offset.x + size.x) > isize.x) return {};
	if ((offset.y + size.y) > isize.y) return {};

	lak::image<lak::vec3f_t> dst;
	dst.resize(size);

	for (size_t y = 0, iy = offset.y; y < size.y; ++y, ++iy)
	{
		tasks.push(
		  [&, y = y, iy = iy]()
		  {
			  for (size_t x = 0, ix = offset.x; x < size.x; ++x, ++ix)
				  dst[{x, y}] = src[{ix, iy}];
		  });
	}

	tasks.await();

	return dst;
}

lak::image<lak::vec3f_t> rye::crop(const lak::image<lak::vec3f_t> &src,
                                   lak::vec2s_t offset,
                                   lak::vec2s_t size)
{
	lak::tasks tasks{0U};
	return rye::crop(tasks, src, offset, size);
}

lak::image<lak::vec3f_t> rye::demosaic_bayer(
  lak::tasks &tasks,
  const lak::image<lak::vec3f_t> &src,
  lak::span<const lak::vec2s_t, 4U> channel_coords,
  rye::image_flip_t flip)
{
	auto maybe_transpose = transform_coords(rye::image_flip_t::transpose & flip);
	auto maybe_reverse = transform_coords(rye::image_flip_t::reverse_xy & flip);
	auto maybe_reverse_transpose = transform_coords(flip);

	const lak::vec2s_t isize = src.size() - lak::vec2s_t{4U, 4U};

	lak::image<lak::vec3f_t> dst;
	dst.resize(maybe_transpose(isize, {0U, 0U}));

	/*
	R G R G
	G B G B
	R G R G
	G B G B
	*/

	for (size_t y = 0; y < isize.y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < isize.x; ++x)
			  {
				  const lak::vec2s_t xy_dst =
				    maybe_reverse_transpose({x, y}, dst.size());
				  const lak::vec2s_t xy_src{x + 2U, y + 2U};

				  /*
				  R . R .
				  . . . .
				  R . R .
				  . . . .
				  */

				  if (bool r_x = ((x % 2U) == channel_coords[0U].x),
				      r_y      = ((y % 2U) == channel_coords[0U].y);
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

				  if (bool b_x = ((x % 2U) == channel_coords[2U].x),
				      b_y      = ((y % 2U) == channel_coords[2U].y);
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
				      (xy_m2 == channel_coords[1U]) || (xy_m2 == channel_coords[3U]))
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

	return dst;
}

lak::image<lak::vec3f_t> rye::demosaic_bayer(
  const lak::image<lak::vec3f_t> &src,
  lak::span<const lak::vec2s_t, 4U> channel_coords,
  rye::image_flip_t flip)
{
	lak::tasks tasks{0U};
	return rye::demosaic_bayer(tasks, src, channel_coords, flip);
}

lak::image<lak::vec3f_t> rye::demosaic_xtrans(
  lak::tasks &tasks,
  const lak::image<lak::vec3f_t> &src,
  rye::image_flip_t flip)
{
	auto maybe_transpose = transform_coords(rye::image_flip_t::transpose & flip);
	auto maybe_reverse = transform_coords(rye::image_flip_t::reverse_xy & flip);
	auto maybe_reverse_transpose = transform_coords(flip);

	const lak::vec2s_t isize = src.size() - lak::vec2s_t{6U, 6U};

	lak::image<lak::vec3f_t> dst;
	dst.resize(maybe_transpose(isize, {0U, 0U}));

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
				    maybe_reverse_transpose({x3 + 0U, y3 + 0U}, dst.size());
				  const lak::vec2s_t xy_dst_01 =
				    maybe_reverse_transpose({x3 + 0U, y3 + 1U}, dst.size());
				  const lak::vec2s_t xy_dst_02 =
				    maybe_reverse_transpose({x3 + 0U, y3 + 2U}, dst.size());

				  const lak::vec2s_t xy_dst_10 =
				    maybe_reverse_transpose({x3 + 1U, y3 + 0U}, dst.size());
				  const lak::vec2s_t xy_dst_11 =
				    maybe_reverse_transpose({x3 + 1U, y3 + 1U}, dst.size());
				  const lak::vec2s_t xy_dst_12 =
				    maybe_reverse_transpose({x3 + 1U, y3 + 2U}, dst.size());

				  const lak::vec2s_t xy_dst_20 =
				    maybe_reverse_transpose({x3 + 2U, y3 + 0U}, dst.size());
				  const lak::vec2s_t xy_dst_21 =
				    maybe_reverse_transpose({x3 + 2U, y3 + 1U}, dst.size());
				  const lak::vec2s_t xy_dst_22 =
				    maybe_reverse_transpose({x3 + 2U, y3 + 2U}, dst.size());

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
				  dst[xy_dst_20].g =
				    (src[xy_src_10_22].g + src[xy_src_11_10].g + src[xy_src_21_00].g) /
				    3.f;

				  dst[xy_dst_01].g = src[xy_src_11_01].g;
				  dst[xy_dst_11].g = src[xy_src_11_11].g;
				  dst[xy_dst_21].g =
				    (src[xy_src_11_22].g + src[xy_src_11_11].g + src[xy_src_21_01].g) /
				    3.f;

				  dst[xy_dst_02].g =
				    (src[xy_src_01_22].g + src[xy_src_11_01].g + src[xy_src_12_00].g) /
				    3.f;
				  dst[xy_dst_12].g =
				    (src[xy_src_11_22].g + src[xy_src_11_11].g + src[xy_src_12_10].g) /
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

	return dst;
}

lak::image<lak::vec3f_t> rye::demosaic_xtrans(
  const lak::image<lak::vec3f_t> &src, rye::image_flip_t flip)
{
	lak::tasks tasks{0U};
	return rye::demosaic_xtrans(tasks, src, flip);
}

lak::image<lak::vec3f_t> rye::demosaic_foveon(
  lak::tasks &tasks,
  const lak::image<lak::vec3f_t> &src,
  rye::image_flip_t flip)
{
	auto maybe_transpose = transform_coords(rye::image_flip_t::transpose & flip);
	auto maybe_reverse = transform_coords(rye::image_flip_t::reverse_xy & flip);
	auto maybe_reverse_transpose = transform_coords(flip);

	const lak::vec2s_t isize = src.size();

	lak::image<lak::vec3f_t> dst;
	dst.resize(maybe_transpose(isize, {0U, 0U}));

	for (size_t y = 0U; y < isize.y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0U; x < isize.x; ++x)
			  {
				  const lak::vec2s_t xy_dst =
				    maybe_reverse_transpose({x, y}, dst.size());
				  const lak::vec2s_t xy_src{x, y};
				  dst[xy_dst] = src[xy_src];
			  }
		  });
	}

	tasks.await();

	return dst;
}

lak::image<lak::vec3f_t> rye::demosaic_foveon(
  const lak::image<lak::vec3f_t> &src, rye::image_flip_t flip)
{
	lak::tasks tasks{0U};
	return rye::demosaic_foveon(tasks, src, flip);
}

lak::vec3f_t rye::clip_max_rgb(lak::vec3f_t rgb)
{
	return {std::min(rgb.r, 1.f), std::min(rgb.g, 1.f), std::min(rgb.b, 1.f)};
}

lak::vec3f_t rye::clip_min_rgb(lak::vec3f_t rgb)
{
	return {std::max(rgb.r, 0.f), std::max(rgb.g, 0.f), std::max(rgb.b, 0.f)};
}

lak::vec3f_t rye::clamp_rgb(lak::vec3f_t rgb)
{
	const float max = rye::vec_max(rgb);
	if (max > 1.f) rgb /= max;
	return rye::clip_min_rgb(rgb);
}

// https://www.niwa.nu/2013/05/math-behind-colorspace-conversions-rgb-hsl/
lak::vec3f_t rye::rgb_to_hsl(lak::vec3f_t rgb)
{
	rgb              = rye::clamp_rgb(rgb);
	const float max  = rye::vec_max(rgb);
	const float min  = rye::vec_min(rgb);
	const float diff = max - min;
	const float L    = (min + max) / 2.f;
	const float S    = min == max ? 0.f
	                   : L < 0.5f ? diff / (max + min)
	                              : diff / (2.f - max - min);
	const float H    = S == 0.f
	                     ? 0.f
	                     : (max == rgb.r   ? (rgb.g - rgb.b) / diff
	                        : max == rgb.g ? 2.f + ((rgb.b - rgb.r) / diff)
	                                       : 4.f + ((rgb.r - rgb.g) / diff)) /
                        6.f;
	return {lak::fpmod(H, 1.f), S, L};
}

lak::vec3f_t rye::hsl_to_rgb(lak::vec3f_t hsl)
{
	const float H = lak::fpmod(hsl.r, 1.f);
	const float S = hsl.g;
	const float L = hsl.b;
	if (S == 0.f) return {L, L, L};

	const float t1 = L < 0.5f ? L * (1.f + S) : (L + S) - (L * S);
	const float t2 = (2.f * L) - t1;
	const float tr = H + (1.f / 3.f);
	const float tg = H;
	const float tb = H + (2.f / 3.f);

	auto transform = [&](float v) -> float
	{
		if (v > 1.f) v -= 1.f;
		if (v * 6 < 1.f)
			return t2 + (t1 - t2) * 6.f * v;
		else if (v * 2.f < 1.f)
			return t1;
		else if (v * 3.f < 2.f)
			return t2 + (t1 - t2) * ((2.f / 3.f) - v) * 6;
		else
			return t2;
	};

	return {transform(tr), transform(tg), transform(tb)};
}

lak::vec3f_t rye::white_balance(lak::vec3f_t white_point)
{
	float mid = (rye::vec_max(white_point) + rye::vec_min(white_point)) / 2.f;
	return {mid / white_point.r, mid / white_point.g, mid / white_point.b};
}

lak::vec3f_t rye::exp_correction(lak::vec3f_t colour,
                                 float exposure,
                                 float lightness,
                                 float contrast,
                                 float saturation)
{
	exposure /= 10.f;
	lightness /= 10.f;
	lightness += 1.f;
	contrast /= 10.f;
	contrast += 1.f;
	saturation /= 10.f;
	saturation += 1.f;

	colour *= std::exp(exposure);

	auto [H, S, L] = rye::rgb_to_hsl(colour);

	L = lak::sigmoid(-10.f / lightness, L);

	L = (lak::sigmoid(-10.f / contrast, (L * 2.f) - 1.f) + 1.f) / 2.f;

	L = std::min(L, 1.f);

	S *= saturation;

	return rye::hsl_to_rgb({H, S, L});
}

float rye::to_srgb(float value)
{
	if (value <= 0.0031308f)
		return 12.92f * value;
	else
		return (1.055f * std::pow(value, 1.f / 2.4f)) - 0.055f;
}

lak::vec3f_t rye::to_srgb(lak::vec3f_t colour)
{
	return {
	  rye::to_srgb(colour.r), rye::to_srgb(colour.g), rye::to_srgb(colour.b)};
}
