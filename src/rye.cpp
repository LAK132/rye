#include "rye.hpp"

#include <lak/tasks.hpp>

void rye_image_view(ImTextureRef texture, float *scale)
{
	ImGui::DragFloat("Scale", scale, 0.01f, 0.1f, 10.0f);
	ImGui::Separator();
	rye_image_view(texture, *scale);
}

void rye_image_view(ImTextureRef texture, const float scale)
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

lak::vec3f_t rye_desqueeze_sample(const lak::image<lak::vec3f_t> &src,
                                  lak::vec2s_t dst_size,
                                  lak::vec2s_t dst_coord)
{
	return rye_desqueeze_sampler(src, dst_size)(dst_coord);
}

lak::image<lak::vec3f_t> rye_desqueeze(const lak::image<lak::vec3f_t> &img,
                                       float desqueeze)
{
	lak::image<lak::vec3f_t> result;

	result.resize(
	  {static_cast<size_t>(std::llround(double(img.size().x) * desqueeze)),
	   img.size().y});

	auto sampler = rye_desqueeze_sampler(img, result.size());

	for (lak::vec2s_t xy = {0U, 0U}; xy.y < result.size().y; ++xy.y)
		for (xy.x = 0U; xy.x < result.size().x; ++xy.x) result[xy] = sampler(xy);

	return result;
}

lak::image<lak::vec3f_t> rye_waveform(const lak::image<lak::vec3f_t> &img)
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

	lak::tasks tasks{lak::tasks::hardware_max()};

	for (size_t y = 0; y < img.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < result.size().x; ++x)
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

lak::array<lak::vec3f_t> rye_histogram(const lak::image<lak::vec3f_t> &img)
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

	lak::tasks tasks{lak::tasks::hardware_max()};

	for (size_t y = 0; y < img.size().y; ++y)
	{
		tasks.push(
		  [&, y = y]()
		  {
			  for (size_t x = 0; x < img.size().x; ++x)
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

lak::vec3f_t rye_clip_max_rgb(lak::vec3f_t rgb)
{
	return {std::min(rgb.r, 1.f), std::min(rgb.g, 1.f), std::min(rgb.b, 1.f)};
}

lak::vec3f_t rye_clip_min_rgb(lak::vec3f_t rgb)
{
	return {std::max(rgb.r, 0.f), std::max(rgb.g, 0.f), std::max(rgb.b, 0.f)};
}

lak::vec3f_t rye_clamp_rgb(lak::vec3f_t rgb)
{
	const float max = rye_vec_max(rgb);
	if (max > 1.f) rgb /= max;
	return rye_clip_min_rgb(rgb);
}

// https://www.niwa.nu/2013/05/math-behind-colorspace-conversions-rgb-hsl/
lak::vec3f_t rye_rgb_to_hsl(lak::vec3f_t rgb)
{
	rgb              = rye_clamp_rgb(rgb);
	const float max  = rye_vec_max(rgb);
	const float min  = rye_vec_min(rgb);
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

lak::vec3f_t rye_hsl_to_rgb(lak::vec3f_t hsl)
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

lak::vec3f_t rye_white_balance(lak::vec3f_t white_point)
{
	float mid = (rye_vec_max(white_point) + rye_vec_min(white_point)) / 2.f;
	return {mid / white_point.r, mid / white_point.g, mid / white_point.b};
}

lak::vec3f_t rye_exp_correction(lak::vec3f_t colour,
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

	auto [H, S, L] = rye_rgb_to_hsl(colour);

	L = lak::sigmoid(-10.f / lightness, L);

	L = (lak::sigmoid(-10.f / contrast, (L * 2.f) - 1.f) + 1.f) / 2.f;

	L = std::min(L, 1.f);

	S *= saturation;

	return rye_hsl_to_rgb({H, S, L});
}

float rye_to_srgb(float value)
{
	if (value <= 0.0031308f)
		return 12.92f * value;
	else
		return (1.055f * std::pow(value, 1.f / 2.4f)) - 0.055f;
}

lak::vec3f_t rye_to_srgb(lak::vec3f_t colour)
{
	return {rye_to_srgb(colour.r), rye_to_srgb(colour.g), rye_to_srgb(colour.b)};
}
