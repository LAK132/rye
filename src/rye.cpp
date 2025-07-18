#include "rye.hpp"

rye_texture rye_create_texture(const lak::image4_t &bitmap,
                               const lak::graphics_mode mode)
{
	if (mode == lak::graphics_mode::OpenGL)
	{
		lak::opengl::texture result(GL_TEXTURE_2D);
		result.bind()
		  .apply(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER)
		  .apply(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER)
		  .apply(GL_TEXTURE_MIN_FILTER, GL_LINEAR)
		  .apply(GL_TEXTURE_MAG_FILTER, GL_NEAREST)
		  .build(0,
		         GL_RGBA,
		         (lak::vec2<GLsizei>)bitmap.size(),
		         0,
		         GL_RGBA,
		         GL_UNSIGNED_BYTE,
		         bitmap.data());

		return result;
	}
	else if (mode == lak::graphics_mode::Software)
	{
		texture_color32_t result;
		result.copy(bitmap.size().x, bitmap.size().y, (color32_t *)bitmap.data());
		return result;
	}
	else
	{
		FATAL("Unknown graphics mode: ", (uintmax_t)mode);
		// return lak::monostate{};
	}
}

rye_texture rye_create_texture(const lak::image<lak::vec3f_t> &bitmap,
                               const lak::graphics_mode mode)
{
	if (mode == lak::graphics_mode::OpenGL)
	{
		lak::opengl::texture result(GL_TEXTURE_2D);
		result.bind()
		  .apply(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER)
		  .apply(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER)
		  .apply(GL_TEXTURE_MIN_FILTER, GL_LINEAR)
		  .apply(GL_TEXTURE_MAG_FILTER, GL_NEAREST)
		  .build(0,
		         GL_RGB,
		         (lak::vec2<GLsizei>)bitmap.size(),
		         0,
		         GL_RGB,
		         GL_FLOAT,
		         bitmap.data());

		return result;
	}
	else if (mode == lak::graphics_mode::Software)
	{
		texture_color32_t result;
		result.init(bitmap.size().x, bitmap.size().y);
		for (size_t y = 0; y < bitmap.size().y; ++y)
			for (size_t x = 0; x < bitmap.size().x; ++x)
			{
				result.at(x, y).r = uint8_t(std::min<uint64_t>(
				  uint64_t(bitmap[lak::vec2s_t{x, y}].r * 256), 255));
				result.at(x, y).g = uint8_t(std::min<uint64_t>(
				  uint64_t(bitmap[lak::vec2s_t{x, y}].g * 256), 255));
				result.at(x, y).b = uint8_t(std::min<uint64_t>(
				  uint64_t(bitmap[lak::vec2s_t{x, y}].b * 256), 255));
				result.at(x, y).a = 255;
			}
		return result;
	}
	else
	{
		FATAL("Unknown graphics mode: ", (uintmax_t)mode);
		// return lak::monostate{};
	}
}

rye_texture rye_create_texture(const lak::image<float> &bitmap,
                               const lak::graphics_mode mode)
{
	if (mode == lak::graphics_mode::OpenGL)
	{
		lak::opengl::texture result(GL_TEXTURE_2D);
		result.bind()
		  .apply(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER)
		  .apply(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER)
		  .apply(GL_TEXTURE_MIN_FILTER, GL_LINEAR)
		  .apply(GL_TEXTURE_MAG_FILTER, GL_NEAREST)
		  .build(0,
		         GL_RED,
		         (lak::vec2<GLsizei>)bitmap.size(),
		         0,
		         GL_RED,
		         GL_FLOAT,
		         bitmap.data());

		return result;
	}
	else if (mode == lak::graphics_mode::Software)
	{
		texture_color32_t result;
		result.init(bitmap.size().x, bitmap.size().y);
		for (size_t y = 0; y < bitmap.size().y; ++y)
			for (size_t x = 0; x < bitmap.size().x; ++x)
			{
				result.at(x, y).r = uint8_t(
				  std::min<uint64_t>(uint64_t(bitmap[lak::vec2s_t{x, y}] * 256), 255));
				result.at(x, y).g = 0;
				result.at(x, y).b = 0;
				result.at(x, y).a = 255;
			}
		return result;
	}
	else
	{
		FATAL("Unknown graphics mode: ", (uintmax_t)mode);
		// return lak::monostate{};
	}
}

void rye_image_view(const rye_texture &texture, float *scale)
{
	ImGui::DragFloat("Scale", scale, 0.1f, 0.1f, 10.0f);
	ImGui::Separator();
	rye_image_view(texture, *scale);
}

void rye_image_view(const rye_texture &texture, const float scale)
{
	ImGui::BeginChild("Image View",
	                  ImVec2(0, 0),
	                  false,
	                  ImGuiWindowFlags_NoSavedSettings |
	                    ImGuiWindowFlags_AlwaysVerticalScrollbar |
	                    ImGuiWindowFlags_AlwaysHorizontalScrollbar);

	// :TODO: double check that the window is in the correct graphics mode

	if (const auto glimg = texture.template get<lak::opengl::texture>(); glimg)
	{
		if (!glimg->get())
		{
			ImGui::Text("No image selected.");
		}
		else
		{
			ImGui::Image((ImTextureID)(uintptr_t)glimg->get(),
			             ImVec2(scale * (float)glimg->size().x,
			                    scale * (float)glimg->size().y));
		}
	}
	else if (const auto srimg = texture.template get<texture_color32_t>(); srimg)
	{
		if (!srimg->pixels)
		{
			ImGui::Text("No image selected.");
		}
		else
		{
			ImGui::Image((ImTextureID)(uintptr_t)&srimg,
			             ImVec2(scale * (float)srimg->w, scale * (float)srimg->h));
		}
	}
	else if (texture.template holds<lak::monostate>())
	{
		ImGui::Text("No image selected.");
	}
	else
	{
		ERROR("Invalid texture type");
	}

	ImGui::EndChild();
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
