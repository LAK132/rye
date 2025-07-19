#include <lak/opengl/texture.hpp>

#include <lak/imgui/basic_window.hpp>
#include <lak/imgui/widgets.hpp>
#include <misc/softraster/texture.h>

using rye_texture =
  lak::variant<lak::monostate, lak::opengl::texture, texture_color32_t>;

rye_texture rye_create_texture(const lak::image4_t &bitmap,
                               const lak::graphics_mode mode);

rye_texture rye_create_texture(const lak::image<lak::vec3f_t> &bitmap,
                               const lak::graphics_mode mode);

rye_texture rye_create_texture(const lak::image<float> &bitmap,
                               const lak::graphics_mode mode);

void rye_image_view(const rye_texture &texture, float *scale);

void rye_image_view(const rye_texture &texture, const float scale);

template<typename T>
T rye_vec_max(lak::vec3<T> vec)
{
	return std::max<T>(std::max<T>(vec.r, vec.g), vec.b);
}

template<typename T>
T rye_vec_min(lak::vec3<T> vec)
{
	return std::min<T>(std::min<T>(vec.r, vec.g), vec.b);
}

lak::vec3f_t rye_clip_max_rgb(lak::vec3f_t rgb);

lak::vec3f_t rye_clip_min_rgb(lak::vec3f_t rgb);

lak::vec3f_t rye_clamp_rgb(lak::vec3f_t rgb);

lak::vec3f_t rye_rgb_to_hsl(lak::vec3f_t rgb);

lak::vec3f_t rye_hsl_to_rgb(lak::vec3f_t hsl);

lak::vec3f_t rye_exp_correction(lak::vec3f_t colour,
                                float exposure,
                                float lightness,
                                float contrast,
                                float saturation);

float rye_to_srgb(float value);

lak::vec3f_t rye_to_srgb(lak::vec3f_t colour);

inline auto rye_relative_blackbody(float colour_temp)
{
	return
	  [colour_temp, blackbody_max = lak::blackbody_peak_radiance(colour_temp)](
	    double wavelength)
	{
		return float(lak::blackbody_radiance(wavelength, colour_temp) /
		             blackbody_max);
	};
}
