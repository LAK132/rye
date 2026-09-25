#include <lak/system/opengl/texture.hpp>

#include <lak/imgui/basic_window.hpp>
#include <lak/imgui/widgets.hpp>
#include <lak/softrender/texture.hpp>

#include <lak/tasks.hpp>

namespace rye
{
	enum struct sensor_format_t
	{
		bayer,
		xtrans,
		foveon,
		rd175,
	};

	enum struct image_flip_t : int
	{
		none       = 0,
		reverse_y  = 1,
		reverse_x  = 2,
		reverse_xy = 3,
		transpose  = 4,
	};
}

constexpr inline rye::image_flip_t operator&(rye::image_flip_t a,
                                             rye::image_flip_t b)
{
	return static_cast<rye::image_flip_t>(static_cast<int>(a) &
	                                      static_cast<int>(b));
}
constexpr inline rye::image_flip_t operator|(rye::image_flip_t a,
                                             rye::image_flip_t b)
{
	return static_cast<rye::image_flip_t>(static_cast<int>(a) |
	                                      static_cast<int>(b));
}

namespace rye
{
	void image_view(ImTextureRef texture, float *scale);

	void image_view(ImTextureRef texture, const float scale);

	inline auto desqueeze_sampler(const lak::image<lak::vec3f_t> &src,
	                              lak::vec2s_t dst_size)
	{
		ASSERT_EQUAL(src.size().y, dst_size.y);
		ASSERT_GREATER_OR_EQUAL(dst_size.x, src.size().x);

		return [&src,
		        dst_size,
		        sample_rate = double(src.size().x) /
		                      double(dst_size.x)](lak::vec2s_t dst_coord)
		{
			double x = double(dst_coord.x) * sample_rate;
			size_t y = dst_coord.y;

			double x_min = std::floor(x);
			double x_max = std::ceil(x);
			size_t a     = static_cast<size_t>(std::llround(x_min));
			size_t b     = static_cast<size_t>(std::llround(x_max));

			if (a == b) return src[{a, y}];

			return (src[{a, y}] * float(x_max - x)) +
			       (src[{b, y}] * float(x - x_min));
		};
	}

	lak::vec3f_t desqueeze_sample(const lak::image<lak::vec3f_t> &src,
	                              lak::vec2s_t dst_size,
	                              lak::vec2s_t dst_coord);

	lak::image<lak::vec3f_t> desqueeze(const lak::image<lak::vec3f_t> &img,
	                                   float desqueeze);
	lak::image<lak::vec3f_t> desqueeze(lak::tasks &tasks,
	                                   const lak::image<lak::vec3f_t> &img,
	                                   float desqueeze);

	lak::image<lak::vec3f_t> waveform(const lak::image<lak::vec3f_t> &img);
	lak::image<lak::vec3f_t> waveform(lak::tasks &tasks,
	                                  const lak::image<lak::vec3f_t> &img);

	lak::array<lak::vec3f_t> histogram(const lak::image<lak::vec3f_t> &img);
	lak::array<lak::vec3f_t> histogram(lak::tasks &tasks,
	                                   const lak::image<lak::vec3f_t> &img);

	template<typename T>
	T vec_max(lak::vec3<T> vec)
	{
		return std::max<T>(std::max<T>(vec.r, vec.g), vec.b);
	}

	template<typename T>
	T vec_min(lak::vec3<T> vec)
	{
		return std::min<T>(std::min<T>(vec.r, vec.g), vec.b);
	}

	lak::vec3f_t clip_max_rgb(lak::vec3f_t rgb);

	lak::vec3f_t clip_min_rgb(lak::vec3f_t rgb);

	lak::vec3f_t clamp_rgb(lak::vec3f_t rgb);

	lak::vec3f_t rgb_to_hsl(lak::vec3f_t rgb);

	lak::vec3f_t hsl_to_rgb(lak::vec3f_t hsl);

	lak::vec3f_t white_balance(lak::vec3f_t white_point);

	lak::vec3f_t exp_correction(lak::vec3f_t colour,
	                            float exposure,
	                            float lightness,
	                            float contrast,
	                            float saturation);

	float to_srgb(float value);

	lak::vec3f_t to_srgb(lak::vec3f_t colour);

	inline auto relative_blackbody(float colour_temp)
	{
		return
		  [colour_temp, blackbody_max = lak::blackbody_peak_radiance(colour_temp)](
		    double wavelength)
		{
			return float(lak::blackbody_radiance(wavelength, colour_temp) /
			             blackbody_max);
		};
	}
}