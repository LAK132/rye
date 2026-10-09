#define LAK_BASIC_PROGRAM_IMGUI_WINDOW_IMPL
#define LAK_BASIC_PROGRAM_IMPLOT_IMPL
#define LAK_BASIC_PROGRAM_IMPLOT3D_IMPL
#include <lak/basic_program.hpp>

#include <lak/system/architecture.hpp>
#include <lak/system/cobalt/math.hpp>
#include <lak/system/cobalt/program.hpp>
#include <lak/system/cobalt/result.hpp>
#include <lak/system/cobalt/state.hpp>

#include "main.hpp"

#include "image_data.hpp"
#include "libraw_format.hpp"
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

#include <lak/col/cie.hpp>
#include <lak/col/oprgb.hpp>
#include <lak/col/prophoto.hpp>
#include <lak/col/srgb.hpp>

#include <lak/imgui/cie.hpp>
#include <lak/imgui/texture.hpp>
#include <lak/imgui/widgets.hpp>

#include <stb_image_write.h>

#include <libraw/libraw.h>

#include <filesystem>

#include <inttypes.h>

#include <lak/basic_program.inl>

#include <unordered_map>

bool force_only_error = false;

lak::fs::path load_path;
lak::fs::path binary_path;
lak::optional<lak::future<lak::result<rye::image_data, lak::u8string>>>
  image_load;
lak::optional<lak::future<void>> binary_load, image_process;
lak::optional<rye::image_data> raw_image;
bool binary_update = false, raw_update = false;

int last_white_level = -1;
lak::ImUniqueTexture lrawtex, lrdebayertex, lrprocessedtex, lrsrgbtex,
  lrwavetex, lrwave2tex, finaltex;

void reset_textures()
{
	lrawtex.reset();
	lrprocessedtex.reset();
	lrdebayertex.reset();
	lrsrgbtex.reset();
	lrwavetex.reset();
	lrwave2tex.reset();
	finaltex.reset();
}

lak::image<lak::vec3f_t> lrdimg, lrpimg, lrsrgbimg, lrwaveimg, lrwave2img,
  lrdupimg;
uint16_t out_colour_temp;

lak::array<lak::vec3f_t> _ir_histo, ir_histo, _white_histo, white_histo,
  _srgb_histo, srgb_histo;

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

void load_binary_async(const lak::fs::path &path)
{
	lrdimg.resize({0, 0});
	lrsrgbimg.resize({0, 0});
	lrwaveimg.resize({0, 0});
	lrwave2img.resize({0, 0});
	lrdupimg.resize({0, 0});
	load_path  = path;
	image_load = rye::load_image_async(path);
}

#if 0
void process_image_ir_stage_1(lak::tasks &tasks,
                              lak::image<lak::vec3f_t> &img,
                              lak::vec3f_t ir_in)
{
	if (raw_image->sensor == rye::sensor_format_t::foveon)
	{
		const lak::mat3f_t ir_channel_swap{
		  lak::vec3f_t{0.f, 0.f, 1.f / ir_in.b},
		  lak::vec3f_t{1.f, 0.f, -ir_in.r / ir_in.b},
		  lak::vec3f_t{0.f, 1.f, -ir_in.g / ir_in.b},
		};

		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  img[{x, y}] = ir_channel_swap * img[{x, y}];
				  }
			  });
		}

		tasks.await();
	}
	else
	{
		const float ir_in_red   = ir_in.r / ir_in.b;
		const float ir_in_green = ir_in.g / ir_in.b;

		const lak::mat3f_t ir_channel_swap{
		  lak::vec3f_t{0.f, 0.f, 1.f},
		  lak::vec3f_t{1.f, 0.f, -ir_in.r / ir_in.b},
		  lak::vec3f_t{0.f, 1.f, -ir_in.g / ir_in.b},
		};

		for (size_t y = 0; y < img.size().y; ++y)
		{
			tasks.push(
			  [&, y = y]()
			  {
				  for (size_t x = 0; x < img.size().x; ++x)
				  {
					  img[{x, y}] = ir_channel_swap * img[{x, y}];
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

	auto wb_wv      = rye::relative_blackbody(colour_temp);
	out_colour_temp = uint16_t(std::max<long long>(
	  0, std::min<long long>((1U << 15U) - 1, std::llround(colour_temp))));

	// camera sensitivity compensation
	const lak::vec3f_t aero_match_balance = rye::white_balance(
	  {1.f / aero_match.r, 1.f / aero_match.g, 1.f / aero_match.b});

	// blackbody whitebalance
	const lak::vec3f_t temp_sensitivity{
	  wb_wv(850.0), wb_wv(600.0), wb_wv(525.0)};
	const lak::vec3f_t temp_balance = rye::white_balance(temp_sensitivity);

	// aerochrome sensitivity factor
	const lak::vec3f_t aerochrome_sensitivity{
	  std::exp(0.5f), std::exp(1.5f), std::exp(1.4f)};
	const lak::vec3f_t aero_balance = rye::white_balance(aerochrome_sensitivity);

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

				  const float min = rye::vec_min<float>(irrgb);
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

	bool is_foveon = raw_image->sensor == rye::sensor_format_t::foveon;
	[[maybe_unused]] bool is_xtrans =
	  raw_image->sensor == rye::sensor_format_t::xtrans;

	lrdimg = raw_image->data;

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
					  p *= raw_image->whitebalance_coef;

					  p = raw_image->cam_to_sRGB * p;
				  }

				  p =
				    rye::exp_correction(p, exposure, lightness, contrast, saturation);
				  p = rye::to_srgb(p);
				  return p;
			  };
			  if (desqueeze)
			  {
				  auto sampler = rye::desqueeze_sampler(lrdimg, lrpimg.size());
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

		lrwaveimg = rye::waveform(wavetemp);
		_ir_histo = rye::histogram(wavetemp);
	}

	process_image_ir_stage_1(tasks, lrdimg, ir_balance.ir_in);

	process_image_ir_stage_2(tasks, lrdimg, ir_balance.aero_match, colour_temp);

	// generate white balance histogram and waveform
	lrwave2img   = rye::waveform(lrdimg);
	_white_histo = rye::histogram(lrdimg);

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
				  auto sampler = rye::desqueeze_sampler(lrdimg, lrsrgbimg.size());
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrsrgbimg.size().x; ++xy.x)
				  {
					  lrsrgbimg[xy] = sampler(xy);
					  lrsrgbimg[xy] = rye::exp_correction(
					    lrsrgbimg[xy], exposure, lightness, contrast, saturation);
					  lrsrgbimg[xy] = rye::to_srgb(lrsrgbimg[xy]);
				  }
			  }
			  else
			  {
				  for (lak::vec2s_t xy = {0, y}; xy.x < lrsrgbimg.size().x; ++xy.x)
				  {
					  lrsrgbimg[xy] = lrdimg[xy];
					  lrsrgbimg[xy] = rye::exp_correction(
					    lrsrgbimg[xy], exposure, lightness, contrast, saturation);
					  lrsrgbimg[xy] = rye::to_srgb(lrsrgbimg[xy]);
				  }
			  }
		  });
	}
	tasks.await();

	// generate final histogram
	_srgb_histo = rye::histogram(lrsrgbimg);
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
#endif

struct rye_gpu_image_process_state
{
	cobalt::graphics::IRenderPassNode *compute_pass_node = nullptr;
	cobalt::graphics::IRenderPassNode *display_pass_node = nullptr;
	cobalt::graphics::IShaderProgram::unique_ptr compute_program;
	cobalt::graphics::IShaderProgram::unique_ptr display_program;
	cobalt::graphics::IVertexBuffer::unique_ptr vertex_buffer;
	cobalt::graphics::IRenderableNode::unique_ptr renderable_node;

	cobalt::graphics::ITexelArray::unique_ptr image_buffer;

	// OpenGL always requires a sampler when dealing with textures
	cobalt::graphics::ITextureSampler2D::unique_ptr sampler;

	lak::cobalt::program_state<
	  lak::type_pack<
	    lak::cobalt::state_value_binding<cobalt::graphics::V2UInt32,
	                                     "image_size">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "camera_to_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "camera_XYZ_to_scene_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "scene_XYZ_to_display_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "XYZ_to_display">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M4Float32,
	                                     "user_matrix">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V3Float32,
	                                     "scene_white_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V1Float32,
	                                     "exposure">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V1Float32,
	                                     "contrast">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V1Float32,
	                                     "lightness">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V1Float32,
	                                     "saturation">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V1Float32, "hue">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "raw_white_balance">,
	    lak::cobalt::state_value_binding<cobalt::graphics::V1Float32,
	                                     "display_gamma">>,
	  lak::type_pack<
	    lak::cobalt::texture_binding<cobalt::graphics::ITextureBuffer2D, "tex">>>
	  compute_bindings;

	lak::cobalt::program_state<
	  lak::type_pack<lak::cobalt::state_value_binding<cobalt::graphics::V2UInt32,
	                                                  "image_size">>>
	  display_bindings;

	rye_gpu_image_process_state() = default;
	rye_gpu_image_process_state(rye_gpu_image_process_state &&other)
	: compute_pass_node(lak::exchange(other.compute_pass_node, nullptr)),
	  display_pass_node(lak::exchange(other.display_pass_node, nullptr)),
	  compute_program(lak::move(other.compute_program)),
	  display_program(lak::move(other.display_program)),
	  vertex_buffer(lak::move(other.vertex_buffer)),
	  renderable_node(lak::move(other.renderable_node)),
	  image_buffer(lak::move(other.image_buffer)),
	  sampler(lak::move(other.sampler)),
	  display_bindings(lak::move(other.display_bindings)),
	  compute_bindings(lak::move(other.compute_bindings))
	{
	}
	rye_gpu_image_process_state &operator=(rye_gpu_image_process_state &&other)
	{
		lak::swap(compute_pass_node, other.compute_pass_node);
		lak::swap(display_pass_node, other.display_pass_node);
		lak::swap(compute_program, other.compute_program);
		lak::swap(display_program, other.display_program);
		lak::swap(vertex_buffer, other.vertex_buffer);
		lak::swap(renderable_node, other.renderable_node);
		lak::swap(image_buffer, other.image_buffer);
		lak::swap(sampler, other.sampler);
		lak::swap(display_bindings, other.display_bindings);
		lak::swap(compute_bindings, other.compute_bindings);
		return *this;
	}
	rye_gpu_image_process_state(const rye_gpu_image_process_state &) = delete;
	rye_gpu_image_process_state &operator=(const rye_gpu_image_process_state &) =
	  delete;

	~rye_gpu_image_process_state() { clear(); }

	void clear()
	{
		if (compute_pass_node)
		{
			compute_pass_node->RemoveAllChildNodes();
			compute_pass_node = nullptr;
		}
		if (compute_bindings.program_node)
		{
			compute_bindings.program_node->RemoveAllChildNodes();
			compute_bindings.program_node.reset();
		}
		if (compute_bindings.state_group_node)
		{
			compute_bindings.state_group_node->RemoveAllChildNodes();
			compute_bindings.state_group_node.reset();
		}
		compute_program.reset();

		if (display_pass_node)
		{
			display_pass_node->RemoveAllChildNodes();
			display_pass_node = nullptr;
		}
		if (display_bindings.program_node)
		{
			display_bindings.program_node->RemoveAllChildNodes();
			display_bindings.program_node.reset();
		}
		if (display_bindings.state_group_node)
		{
			display_bindings.state_group_node->RemoveAllChildNodes();
			display_bindings.state_group_node.reset();
		}
		display_program.reset();

		image_buffer.reset();
		renderable_node.reset();
		vertex_buffer.reset();
	}

	static lak::result<rye_gpu_image_process_state, lak::u8string> make(
	  lak::window &wnd,
	  cobalt::graphics::IRenderPassNode *compute_pass_node,
	  cobalt::graphics::IRenderPassNode *display_pass_node,
	  cobalt::graphics::ITextureBuffer2D *source_texture)
	{
		const auto &cgx = lak::cobalt_graphics_context(wnd.handle()).UNWRAP();
		auto *rd        = cgx.renderer.get();

		rye_gpu_image_process_state state;

		state.compute_pass_node = compute_pass_node;
		state.display_pass_node = display_pass_node;

		auto vs_in  = R"(
struct VSInput
{
	float2 position : position;
	float2 texCoord : texCoord;
};)"_str;
		auto vs_out = R"(
struct VSOutput
{
	float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};)"_str;

		auto vs = vs_in + vs_out + R"(
VSOutput main(VSInput IN)
{
	VSOutput OUT;

	OUT.position = float4(IN.position, 0.0f, 1.0f);
	OUT.texCoord = IN.texCoord;

	return OUT;
})"_str;

		auto fs = vs_out + R"(
Buffer<float4> image_buffer;
uniform uint2 image_size;

float4 get_pixel(int2 pixel)
{
	pixel = clamp(pixel, int2(0, 0), int2(image_size) - 1);
	return image_buffer[pixel.x + (pixel.y * image_size.x)];
}

float4 main(VSOutput IN) : SV_Target
{
	float2 coord = (IN.texCoord * float2(image_size)) - 0.5f;
	int2 index = int2(floor(coord));
	float2 weight = frac(coord);
	float4 p00 = get_pixel(index);
	float4 p01 = get_pixel(index + int2(0, 1));
	float4 p10 = get_pixel(index + int2(1, 0));
	float4 p11 = get_pixel(index + int2(1, 1));
	float4 p0 = lerp(p00, p01, weight.y);
	float4 p1 = lerp(p10, p11, weight.y);
	return lerp(p0, p1, weight.x);
})"_str;

		auto cs = R"(
uniform Texture2D<float4> tex;
RWBuffer<float4> image_buffer;
uniform uint2 image_size;

uniform row_major float3x3 camera_to_XYZ;
uniform row_major float3x3 camera_XYZ_to_scene_XYZ;
uniform row_major float3x3 scene_XYZ_to_display_XYZ;
uniform row_major float3x3 XYZ_to_display;
uniform row_major float4x4 user_matrix;
uniform row_major float3x3 raw_white_balance;
uniform float3 scene_white_XYZ;
uniform float exposure;
uniform float contrast;
uniform float lightness;
uniform float saturation;
uniform float hue;
uniform float display_gamma;

float3 XYZ_to_RGB(float3 colour)
{
	static const float3x3 _XYZ_to_RGB = {
		 2.36461384f, -0.89654057f, -0.46807328f,
		-0.51516621f,  1.4264081f,   0.0887581f,
		 0.0052037f,  -0.01440816f,  1.00920446f};
	return _XYZ_to_RGB * colour;
}

float3 xyY_to_XYZ(float3 colour)
{
	float Yy = colour.z / colour.y;
	float z = 1.0f - (colour.x + colour.y);
	return float3(Yy * colour.x, colour.z, Yy * z);
}

float3 XYZ_to_xyY(float3 colour)
{
	float sum = colour.x + colour.y + colour.z;
	return float3(colour.x / sum, colour.y / sum, colour.y);
}

float2 uv_to_xy(float2 colour)
{
	float divisor = (6.0f * colour.x) + (-16.0f * colour.y) + 12.0f;
	return float2(
		(9.0f * colour.x) / divisor,
		(4.0f * colour.y) / divisor);
}

float2 xy_to_uv(float2 colour)
{
	float divisor = (-2.0f * colour.x) + (12.0f * colour.y) + 3.0f;
	return float2(
		(4.0f * colour.x) / divisor,
		(9.0f * colour.y) / divisor);
}

float3 XYZ_to_uvY(float3 colour)
{
	return float3(xy_to_uv(XYZ_to_xyY(colour).xy), colour.y);
}

float3 uvY_to_XYZ(float3 colour)
{
	return xyY_to_XYZ(float3(uv_to_xy(colour.xy), colour.z));
}

static const float2 D50_xy = float2(0.34567f, 0.35850f);
static const float3 D50_XYZ = xyY_to_XYZ(float3(D50_xy, 1.0f));
static const float3 D50_uvY = XYZ_to_uvY(D50_XYZ);
static const float3 white_point_uvY = float3(0.2009f, 0.4610f, 1.0f);

float3 XYZ_to_Luv(float3 colour, float3 white_uvY)
{
	// http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_Luv.html
	static const float e = 0.008856f;
	static const float k = 903.3f;

	colour = float3(xy_to_uv(XYZ_to_xyY(colour).xy), colour.y);

	// uvY_to_Luv
	float YYn = colour.z / white_uvY.z;
	float L = (YYn <= e)
		? (k * YYn)
		: ((116.0f * pow(YYn, 1.0f / 3.0f)) - 16.0f);
	float L13 = 13.0f * L;
	return float3(
		L,
		L13 * (colour.x - white_uvY.x),
		L13 * (colour.y - white_uvY.y));
}

float3 Luv_to_XYZ(float3 colour, float3 white_uvY)
{
	float u = ((colour.y / colour.x) / 13.0f) + white_uvY.x;
	float v = ((colour.z / colour.x) / 13.0f) + white_uvY.y;
	float Y = (colour.x <= 8.0f)
		? (pow(3.0f / 29.0f, 3.0f) * colour.x * white_uvY.z)
		: (pow((colour.x + 16.0f) / 116.0f, 3.0f) * white_uvY.z);
	return float3(
		((9.0f * u) / (4.0f * v)) * Y,
		Y,
		(((-3.0f * u) + (-20.0f * v) + 12.0f) / (4.0f * v)) * Y);
}

float3 LCh_to_Luv(float3 colour)
{
	return float3(
		colour.x,
		colour.y * cos(colour.z),
		colour.y * sin(colour.z));
}

float3 Luv_to_LCh(float3 colour)
{
	return float3(
		colour.x,
		sqrt(dot(colour.yz, colour.yz)),
		atan2(colour.z, colour.y));
}

float _half_sigmoid(float k, float t)
{
	return (k * t) / (1.0f + k - t);
}

float _sigmoid(float k, float t)
{
	return _half_sigmoid(k, min(abs(t), 1.0f)) * sign(t);
}

float sigmoid(float k, float t)
{
	k = (k < 0.0f) ? min(k, -1.0001f) : max(k, 0.0001f);
	return _sigmoid(k, t);
}

[numthreads(16, 16, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
	if (any(thread_id.xy >= image_size)) return;

	float3 colour = tex.Load(int3(thread_id.xy, 0)).xyz;
	colour = raw_white_balance * colour;
	float4 colour4 = user_matrix * float4(colour, 1.0f);
	colour = colour4.xyz / colour4.w;
	colour = camera_to_XYZ * colour;
	colour = camera_XYZ_to_scene_XYZ * colour;

	colour *= exp(exposure);

	colour = XYZ_to_Luv(colour, XYZ_to_uvY(scene_white_XYZ));
	colour = Luv_to_LCh(colour);

	colour.x /= 100.0f;
	// colour.x *= exp(exposure);
	colour.x = sigmoid(-10.0f / ((lightness / 10.0f) + 1.0f), colour.x);
	colour.x = (sigmoid(-10.0f / ((contrast / 10.0f) + 1.0f),
	                    (colour.x * 2.0f) - 1.0f) + 1.0f) / 2.0f;
	// colour.x = smoothstep(0.0f, 1.0f, colour.x);
	colour.x *= 100.0f;

	colour.y *= exp(saturation / 100.0f);

	// colour.z += radians(hue);

	colour = LCh_to_Luv(colour);
	colour = Luv_to_XYZ(colour, XYZ_to_uvY(scene_white_XYZ));

	colour = scene_XYZ_to_display_XYZ * colour;
	colour = XYZ_to_display * colour;

	image_buffer[thread_id.y * image_size.x + thread_id.x] = float4(
		pow(colour.x, 1.0f/display_gamma),
		pow(colour.y, 1.0f/display_gamma),
		pow(colour.z, 1.0f/display_gamma),
		1.0f);
})"_str;

		state.compute_program = rd->CreateShaderProgram();
		if (!state.compute_program->LoadShaderStage(
		      cobalt::graphics::IShaderProgram::ShaderStage::Compute,
		      lak::cobalt::shader_source_hlsl(cs)))
		{
			ERROR("Loading compute shader stage failed");
			return lak::err_t{u8"Loading compute shader stage failed"_str};
		}
		if (!state.compute_program->CompileProgram())
		{
			ERROR("Failed to compile compute shader");
			return lak::err_t{u8"Failed to compile compute shader"_str};
		}

		state.display_program = rd->CreateShaderProgram();

		if (!state.display_program->LoadShaderStage(
		      cobalt::graphics::IShaderProgram::ShaderStage::Vertex,
		      lak::cobalt::shader_source_hlsl(vs)))
		{
			ERROR("Loading vertex shader stage failed");
			return lak::err_t{u8"Loading vertex shader stage failed"_str};
		}
		if (!state.display_program->LoadShaderStage(
		      cobalt::graphics::IShaderProgram::ShaderStage::Fragment,
		      lak::cobalt::shader_source_hlsl(fs)))
		{
			ERROR("Loading fragment shader stage failed");
			return lak::err_t{u8"Loading fragment shader stage failed"_str};
		}

		if (!state.display_program->CompileProgram())
		{
			ERROR("Failed to compile shader");
			return lak::err_t{u8"Failed to compile shader"_str};
		}

		state.compute_bindings =
		  state.compute_bindings.make(rd, state.compute_program.get()).UNWRAP();
		state.display_bindings =
		  state.display_bindings.make(rd, state.display_program.get()).UNWRAP();

		state.sampler = rd->CreateTextureSampler2D();
		state.sampler->SetTextureFilterMode(
		  cobalt::graphics::ITextureSampler::FilterMode::Linear,
		  cobalt::graphics::ITextureSampler::FilterMode::Nearest);

		const auto image_size = source_texture->MipmapLevelDimensions(0);
		if (image_size.X() == 0U || image_size.Y() == 0U)
			return lak::err_t{u8"Cannot process an empty image"_str};

		state.image_buffer = rd->CreateTexelArray();
		state.image_buffer->SetBufferLayout(
		  cobalt::graphics::ITexelArray::ImageFormat::RGBA,
		  cobalt::graphics::ITexelArray::DataFormat::Float32,
		  size_t(image_size.X()) * size_t(image_size.Y()));
		state.image_buffer->SetUsageFlags(
		  cobalt::graphics::ITexelArray::UsageFlags::ShaderInput |
		  cobalt::graphics::ITexelArray::UsageFlags::ShaderOutput |
		  cobalt::graphics::ITexelArray::UsageFlags::TransferSource);
		if (!state.image_buffer->AllocateMemory())
			return lak::err_t{u8"Failed to allocate processed image"_str};

		const auto compute_output =
		  state.compute_program->GetResourceArrayId("image_buffer");
		const auto display_input =
		  state.display_program->GetResourceArrayId("image_buffer");
		if (compute_output == cobalt::graphics::ResourceArrayId::Null ||
		    display_input == cobalt::graphics::ResourceArrayId::Null)
			return lak::err_t{u8"Failed to bind processed image"_str};

		state.compute_bindings.state_group_node->BindResourceArray(
		  compute_output, state.image_buffer.get());
		state.display_bindings.state_group_node->BindResourceArray(
		  display_input, state.image_buffer.get());
		BOUNDS_ASSERT(state.compute_bindings.template set_texture<"tex">(
		  source_texture, state.sampler.get()));
		BOUNDS_ASSERT(
		  state.compute_bindings.template set_state_value<"image_size">(
		    image_size));
		BOUNDS_ASSERT(
		  state.display_bindings.template set_state_value<"image_size">(
		    image_size));

		state.compute_bindings.state_group_node->SetComputeTask(
		  cobalt::graphics::V3UInt32(
		    (image_size.X() + 15U) / 16U, (image_size.Y() + 15U) / 16U, 1U));
		state.compute_pass_node->AddChildNode(
		  state.compute_bindings.program_node.get());
		state.display_pass_node->AddChildNode(
		  state.display_bindings.program_node.get());

		state.display_bindings.state_group_node->SetPolygonFillMode(
		  cobalt::graphics::IStateGroupNode::PolygonFillMode::Solid);
		state.display_bindings.state_group_node->SetDepthTestEnabled(true);
		state.display_bindings.state_group_node->SetDepthWriteEnabled(true);

		size_t vertex_count = 6;
		lak::array<cobalt::graphics::V2Float32> positions({{-1.0f, -1.0f},
		                                                   {-1.0f, 1.0f},
		                                                   {1.0f, -1.0f},
		                                                   {-1.0f, 1.0f},
		                                                   {1.0f, -1.0f},
		                                                   {1.0f, 1.0f}});
		lak::array<cobalt::graphics::V2Float32> texCoords({{0.0f, 0.0f},
		                                                   {0.0f, 1.0f},
		                                                   {1.0f, 0.0f},
		                                                   {0.0f, 1.0f},
		                                                   {1.0f, 0.0f},
		                                                   {1.0f, 1.0f}});

		cobalt::graphics::VertexAttribute<cobalt::graphics::V2Float32>
		  positions_attribute(
		    vertex_count,
		    cobalt::graphics::IVertexAttribute::PerformanceHint::WriteNever |
		      cobalt::graphics::IVertexAttribute::PerformanceHint::ReadNever,
		    cobalt::graphics::IVertexAttribute::PerformanceHint::WriteNever |
		      cobalt::graphics::IVertexAttribute::PerformanceHint::ReadOften);
		cobalt::graphics::VertexAttribute<cobalt::graphics::V2Float32>
		  texCoords_attribute(
		    vertex_count,
		    cobalt::graphics::IVertexAttribute::PerformanceHint::WriteNever |
		      cobalt::graphics::IVertexAttribute::PerformanceHint::ReadNever,
		    cobalt::graphics::IVertexAttribute::PerformanceHint::WriteNever |
		      cobalt::graphics::IVertexAttribute::PerformanceHint::ReadOften);

		state.vertex_buffer = rd->CreateVertexBuffer();

		if (!state.vertex_buffer->BindVertexAttribute(positions_attribute))
		{
			ERROR("Failed to bind vertex positions attribute");
			return lak::err_t{u8"Failed to bind vertex positions attribute"_str};
		}
		if (!state.vertex_buffer->BindVertexAttribute(texCoords_attribute))
		{
			ERROR("Failed to bind vertex texCoords attribute");
			return lak::err_t{u8"Failed to bind vertex texCoords attribute"_str};
		}

		if (!positions_attribute.SetInitialData(positions.data(),
		                                        positions.size()))
		{
			ERROR("Failed to set initial positions data");
			return lak::err_t{u8"Failed to set initial positions data"_str};
		}
		if (!texCoords_attribute.SetInitialData(texCoords.data(),
		                                        texCoords.size()))
		{
			ERROR("Failed to set initial texCoords data");
			return lak::err_t{u8"Failed to set initial texCoords data"_str};
		}

		if (!state.vertex_buffer->AllocateMemory())
		{
			ERROR("Vertex buffer could not be allocated");
			return lak::err_t{u8"Vertex buffer could not be allocated"_str};
		}

		state.renderable_node = rd->CreateRenderableNode();

		lak::cobalt::as_result(
		  state.renderable_node->SetPrimitiveMode(
		    cobalt::graphics::IRenderableNode::PrimitiveMode::Triangles))
		  .UNWRAP();

		lak::cobalt::as_result(
		  state.renderable_node->BindVertexAttribute(
		    positions_attribute,
		    state.display_program->GetVertexAttributeId("position")))
		  .UNWRAP();
		lak::cobalt::as_result(
		  state.renderable_node->BindVertexAttribute(
		    texCoords_attribute,
		    state.display_program->GetVertexAttributeId("texCoord")))
		  .UNWRAP();

		state.display_bindings.state_group_node->AddChildNode(
		  state.renderable_node.get());

		state.compute_bindings.template set_state_value<"camera_to_XYZ">(
		  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
		state.compute_bindings.template set_state_value<"camera_XYZ_to_scene_XYZ">(
		  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
		state.compute_bindings
		  .template set_state_value<"scene_XYZ_to_display_XYZ">(
		    lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
		state.compute_bindings.template set_state_value<"XYZ_to_display">(
		  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
		state.compute_bindings.template set_state_value<"user_matrix">(
		  lak::cobalt::from_lak(lak::diagonal(lak::vec4f_t(1.f))));
		state.compute_bindings.template set_state_value<"scene_white_XYZ">(
		  lak::cobalt::from_lak(lak::vec3f_t(1.f / 3.f)));
		state.compute_bindings.template set_state_value<"exposure">(0.f);
		state.compute_bindings.template set_state_value<"contrast">(0.f);
		state.compute_bindings.template set_state_value<"lightness">(0.f);
		state.compute_bindings.template set_state_value<"saturation">(0.f);
		state.compute_bindings.template set_state_value<"hue">(0.f);
		state.compute_bindings.template set_state_value<"raw_white_balance">(
		  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
		state.compute_bindings.template set_state_value<"display_gamma">(1.f);

		return lak::move_ok(state);
	}
};

struct rye_window : virtual public basic_window_api
{
	rye_window() : basic_window_api() {}

	const lak::cobalt::graphics_context *gc;

	virtual ~rye_window()
	{
		image_viewport_state.clear();
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
		ASSERT(!!gc->renderer);

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
				img16[xy].r = lak::frac_to_int<uint16_t>(lrdimg[xy].r);
				img16[xy].g = lak::frac_to_int<uint16_t>(lrdimg[xy].g);
				img16[xy].b = lak::frac_to_int<uint16_t>(lrdimg[xy].b);
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
		ifd0.push_Make(
		  lak::astring_view((const char *)raw_image->camera.make.c_str()));
		ifd0.push_Model(
		  lak::astring_view((const char *)raw_image->camera.model.c_str()));
		ifd0.push_Orientation(lak::fixed_array((uint16_t(1U))));
		ifd0.push_SamplesPerPixel(lak::fixed_array(uint16_t(3U)));

		ifd0.push_XResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_YResolution(lak::fixed_array(
		  lak::tiff::urational{.numerator = 300, .denominator = 1}));
		ifd0.push_ResolutionUnit(lak::fixed_array(uint16_t(2U)));
		ifd0.push_Software(APP_NAME ""_view);
		ifd0.push_SampleFormat({1U});

		if (raw_image->shutter != 0.f)
			ifd0.push_ExposureTime(lak::fixed_array(lak::tiff::urational{
			  1000U, uint32_t((1.f / raw_image->shutter) * 1000)}));
		ifd0.push_FNumber(lak::fixed_array(
		  lak::tiff::urational{uint32_t(raw_image->aperture * 100), 100U}));
		ifd0.push_ISOSpeedRatings(lak::fixed_array(uint16_t(raw_image->iso)));
		ifd0.push_FocalLength(lak::fixed_array(
		  lak::tiff::urational{uint32_t(raw_image->focal_length * 100), 100U}));

		ifd0.push_DNGVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_DNGBackwardVersion(
		  lak::fixed_array(uint8_t(1U), uint8_t(4U), uint8_t(1U), uint8_t(0U)));
		ifd0.push_UniqueCameraModel(lak::string_view(
		  lak::fmt<"{} {}">(raw_image->camera.make, raw_image->camera.model)));
		ifd0.push_CameraSerialNumber(
		  lak::astring_view((const char *)raw_image->camera.serial.c_str()));

		auto &exif = ifd0.push_exif();

		exif.push_LensMake(
		  lak::astring_view((const char *)raw_image->lens.make.c_str()));
		exif.push_LensModel(
		  lak::astring_view((const char *)raw_image->lens.model.c_str()));
		exif.push_LensSerialNumber(
		  lak::astring_view((const char *)raw_image->lens.serial.c_str()));

		strm.write<lak::endian::native>(tiff).UNWRAP();

		lak::save_file(path, strm.data);
	}

	const lak::fs::path &file_path() { return binary_path; }

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

	void load_db_data()
	{
		used_ir_balance_from_db = false;
		used_aero_match_from_db = false;
		if (use_database_ir_balance || use_database_aero_match)
		{
			if (auto it = ir_balance_db.find(lak::fmt<"{} {}">(
			      raw_image->camera.make, raw_image->camera.model));
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

	void menu_bar(float frame_time)
	{
		file_menu();
		about_menu(frame_time);
		if (ImGui::Checkbox("Use sensor database IR white point",
		                    &use_database_ir_balance))
			load_db_data();
		if (ImGui::Checkbox("Use sensor database RGB white point",
		                    &use_database_aero_match))
			load_db_data();
	}

	int lraw_white_level = UINT16_MAX;
	rye_ir_balance ir_balance;
	float time_acc      = 0.0f;
	float lraw_contrast = 1.f;
	float colour_temp   = 5000;
	float exposure      = 0.f;
	float lightness     = 0.f;
	float contrast      = 0.f;
	float saturation    = 0.f;
	float hue           = 0.f;
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

	lak::ImUniqueViewport image_viewport;
	rye_gpu_image_process_state image_viewport_state;

	lak::mat3f_t XYZ_to_cam;
	lak::mat3f_t cam_to_XYZ;
	lak::mat3f_t cam_to_sRGB;
	lak::vec4f_t wb_coef;
	lak::mat3f_t raw_wb;
	lak::mat4f_t user_mat = lak::diagonal(lak::vec4f_t(1.f));

	lak::col::cie::xyY_primaries camera_primaries =
	  lak::col::ProPhotoRGB_primaries;
	lak::col::cie::xyY_primaries scene_primaries =
	  lak::col::ProPhotoRGB_primaries;
	lak::col::cie::xyY_primaries display_primaries = lak::col::sRGB_primaries;
	float display_gamma                            = 2.2f;
	// lak::col::cie::xyY_primaries display_primaries{
	//   .r = lak::col::sRGB_primaries.b,
	//   .g = lak::col::sRGB_primaries.r,
	//   .b = lak::col::sRGB_primaries.g,
	//   .w = lak::col::sRGB_primaries.w,
	// };

	enum struct colour_space_method : int
	{
		R_G_B     = 0, // Plain RGB
		IR_R_G    = 1, // Digital false colour IR
		R_G_B_neg = 2, // Colour negative film
		BW_neg    = 3, // B&W negative film
	};

	colour_space_method colour_method = colour_space_method::R_G_B;

	enum struct bw_source_channel : int
	{
		W_channel = 0,
		R_channel = 1,
		G_channel = 2,
		B_channel = 3,
	};

	bw_source_channel bw_source = bw_source_channel::R_channel;

	void calculate_camera_colour_space()
	{
		// switch (colour_method)
		// {
		// 	case colour_space_method::R_G_B:
		// 	{
		// 		auto prim = lak::transpose(cam_to_XYZ);
		// 		camera_primaries.r =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
		// 		camera_primaries.g =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
		// 		camera_primaries.b =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
		// 	}
		// 	break;
		// 	case colour_space_method::IR_R_G:
		// 	{
		// 		auto prim = lak::transpose(lak::inverse(XYZ_to_cam));
		// 		camera_primaries.r =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
		// 		camera_primaries.g =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
		// 		camera_primaries.b =
		// 		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
		// 	}
		// 	break;
		// }
		auto prim = lak::transpose(cam_to_XYZ);
		camera_primaries.r =
		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
		camera_primaries.g =
		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
		camera_primaries.b =
		  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
		camera_primaries.regenerate_w();
		camera_primaries.regenerate_Y();
	}

	void calculate_scene_colour_space()
	{
		// const auto w      = scene_primaries.w;
		// scene_primaries   = camera_primaries;
		// scene_primaries.w = w;
		// scene_primaries =
		//   lak::col::cie::bradford_adapt(scene_primaries, camera_primaries.w);
		// scene_primaries.w = w;
		// scene_primaries =
		//   lak::col::cie::bradford_adapt(camera_primaries, scene_primaries.w);
	}

	void calculate_camera_white_balance()
	{
		switch (colour_method)
		{
			case colour_space_method::R_G_B_neg:
				[[fallthrough]];
			case colour_space_method::BW_neg:
				[[fallthrough]];
			case colour_space_method::R_G_B:
			{
				// colour_temp = std::min(std::max(colour_temp, 4000.f), 25000.f);
				// auto cct    = std::min(
				//   std::max(lak::col::cie::D_series_CCT(colour_temp), 4000.f),
				//   25000.f);
				// auto D50_cam = XYZ_to_cam * lak::col::cie::D50_XYZ.to_vec();
				// auto temp_cam =
				//   XYZ_to_cam * lak::col::cie::to_XYZ(
				//                  lak::col::cie::to_xyY(
				//                    lak::col::cie::D_series_illuminant(cct), 1.f))
				//                  .to_vec();
				// auto cct_wb = wb_coef.xyz() * (D50_cam / temp_cam);
				// raw_wb      = lak::diagonal(cct_wb);
				raw_wb = lak::diagonal(lak::vec3f_t(1.f));
			}
			break;

			case colour_space_method::IR_R_G:
			{
				raw_wb = lak::mat3f_t{
				  lak::vec3f_t(0.f, 0.f, 1.f / ir_balance.ir_in.b),
				  lak::vec3f_t(1.f, 0.f, -(ir_balance.ir_in.r / ir_balance.ir_in.b)),
				  lak::vec3f_t(0.f, 1.f, -(ir_balance.ir_in.g / ir_balance.ir_in.b)),
				};

				// // camera sensitivity compensation
				// const lak::vec3f_t aero_match_balance =
				//   rye::white_balance({1.f / ir_balance.aero_match.r,
				//                       1.f / ir_balance.aero_match.g,
				//                       1.f / ir_balance.aero_match.b});

				// // aerochrome sensitivity factor
				// const lak::vec3f_t aerochrome_sensitivity{
				//   std::exp(0.5f), std::exp(1.5f), std::exp(1.4f)};
				// const lak::vec3f_t aero_balance =
				//   aero_match_balance * rye::white_balance(aerochrome_sensitivity);
				// raw_wb.x *= aero_balance.x;
				// raw_wb.y *= aero_balance.y;
				// raw_wb.z *= aero_balance.z;

				// raw_wb = lak::transpose(raw_wb);

				// // blackbody whitebalance
				// auto wb_wv      = rye::relative_blackbody(colour_temp);
				// const auto g_wb = 1.0 / wb_wv(550.0);
				// const auto r_wb = 1.0 / wb_wv(660.0);
				// const auto i_wb = 1.0 / wb_wv(850.0);
				// const lak::vec3f_t temp_sensitivity{
				//   float(i_wb), float(r_wb), float(g_wb)};
				// const lak::vec3f_t temp_balance =
				// rye::white_balance(temp_sensitivity); raw_wb.x *= temp_balance.y;
				// raw_wb.y *= temp_balance.z;
				// raw_wb.z *= temp_balance.x;

				// raw_wb = lak::transpose(raw_wb);

				// DEBUG(lak::fmt<u8"raw_wb:\n{:-+9.3}\n{:-+9.3}\n{:-+9.3}">(
				//   raw_wb.x, raw_wb.y, raw_wb.z));
			}
			break;

			default:
				ASSERT_UNREACHABLE();
		}
	}

	void update_camera_colour_space()
	{
		image_viewport_state.compute_bindings
		  .template set_state_value<"camera_to_XYZ">(
		    lak::cobalt::from_lak(camera_primaries.linear_to_XYZ()));
		// image_viewport_state.compute_bindings
		//   .template set_state_value<"camera_XYZ_to_scene_XYZ">(
		//     lak::cobalt::from_lak(scene_primaries.linear_to_XYZ() *
		//                           camera_primaries.XYZ_to_linear()));
		image_viewport_state.compute_bindings
		  .template set_state_value<"camera_XYZ_to_scene_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaption_matrix(
		      scene_primaries.w, camera_primaries.w)));
		// image_viewport_state.compute_bindings.template
		// set_state_value<"scene_white_XYZ">(
		//   lak::cobalt::from_lak(scene_primaries.w_XYZ().to_vec()));
		image_viewport_state.compute_bindings
		  .template set_state_value<"scene_white_XYZ">(
		    lak::cobalt::from_lak(camera_primaries.w_XYZ().to_vec()));

		// image_viewport_state.compute_bindings
		//   .template set_state_value<"scene_XYZ_to_display_XYZ">(
		//     lak::cobalt::from_lak(lak::col::cie::bradford_adaption_matrix(
		//       scene_primaries.w, display_primaries.w)));
		image_viewport_state.compute_bindings
		  .template set_state_value<"scene_XYZ_to_display_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaption_matrix(
		      camera_primaries.w, display_primaries.w)));
	}

	void update_display_colour_space()
	{
		image_viewport_state.compute_bindings
		  .template set_state_value<"XYZ_to_display">(
		    lak::cobalt::from_lak(display_primaries.XYZ_to_linear()));

		// image_viewport_state.compute_bindings
		//   .template set_state_value<"scene_XYZ_to_display_XYZ">(
		//     lak::cobalt::from_lak(lak::col::cie::bradford_adaption_matrix(
		//       scene_primaries.w, display_primaries.w)));
		image_viewport_state.compute_bindings
		  .template set_state_value<"scene_XYZ_to_display_XYZ">(
		    lak::cobalt::from_lak(lak::col::cie::bradford_adaption_matrix(
		      camera_primaries.w, display_primaries.w)));
		image_viewport_state.compute_bindings
		  .template set_state_value<"display_gamma">(display_gamma);
	}

	void update_camera_white_balance()
	{
		calculate_camera_white_balance();

		image_viewport_state.compute_bindings
		  .template set_state_value<"raw_white_balance">(
		    lak::cobalt::from_lak(raw_wb));
	}

	void main_region(float frame_time)
	{
		if (image_load)
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

			if (image_load->has_value())
			{
				auto &res = image_load->wait();
				DEFER(image_load.reset());
				if_let_ok (auto &img_data,
				           res.if_err([](const lak::u8string &str) { ERROR(str); }))
				{
					raw_image.emplace(lak::move(img_data));
					binary_path   = load_path;
					binary_update = true;
					raw_update    = true;
					time_acc      = 0.f;

					image_viewport_state.clear();
					reset_textures();

					ir_histo.clear();
					white_histo.clear();
					srgb_histo.clear();

					load_db_data();

					image_viewport.reset();
					finaltex.emplace(raw_image->data);

					wb_coef = lak::vec4f_t(raw_image->whitebalance_coef, 0.f);
					raw_wb  = lak::diagonal(wb_coef.xyz());

					XYZ_to_cam  = raw_image->XYZ_to_cam;
					cam_to_XYZ  = raw_image->cam_to_XYZ;
					cam_to_sRGB = raw_image->cam_to_sRGB;

					calculate_camera_colour_space();
					calculate_camera_white_balance();
					// calculate_scene_colour_space();
					scene_primaries   = camera_primaries;
					display_primaries = camera_primaries;
				}
			}
			ImGui::EndChild();
		}
		else if (!raw_image.has_value())
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
				// lrawtex.emplace(raw_image->data);
				// lrprocessedtex.emplace(lrpimg);
				// lrdebayertex.emplace(lrdimg);
				// lrsrgbtex.emplace(lrsrgbimg);
				// lrwavetex.emplace(lrwaveimg);
				// lrwave2tex.emplace(lrwave2img);
				image_viewport_state.clear();
				image_viewport.reset();
			}

			if (raw_update && !image_process)
			{
				raw_update = false;
				desqueeze  = std::max(.5f, std::min(10.f, desqueeze));
				image_process.reset();
				// process_image_async(lraw_white_level,
				//                     ir_balance,
				//                     colour_temp,
				//                     exposure,
				//                     lightness,
				//                     contrast,
				//                     saturation,
				//                     anamorphic ? lak::make_optional(desqueeze)
				//                                : lak::nullopt);
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

				ImGui::BeginChild("ImgLeft",
				                  {left_size, -1},
				                  true,
				                  ImGuiWindowFlags_NoSavedSettings |
				                    ImGuiWindowFlags_AlwaysVerticalScrollbar);

				const auto left_content_size{ImGui::GetContentRegionAvail()};
				lak::Text<u8"{} {} + {} {}">(raw_image->camera.make,
				                             raw_image->camera.model,
				                             raw_image->lens.make,
				                             raw_image->lens.model);
				lak::Text<u8"ISO {:.0} 1/{:.0}s f/{:.1} {:.1}mm ({:.1}mm)">(
				  raw_image->iso,
				  1.0f / raw_image->shutter,
				  raw_image->aperture,
				  raw_image->focal_length,
				  raw_image->focal_length_35mm == 0.f ? raw_image->focal_length
				                                      : raw_image->focal_length_35mm);

				ImGui::Separator();

				bool update_camera_primaries  = false;
				bool update_scene_primaries   = false;
				bool update_raw_white_balance = false;
				bool update_user_mat          = false;
				{
					const char *labels[4] = {
					  "R G B",
					  "IR R G (Digital)",
					  "R G B negative (Film)",
					  "B&W negative (Film)",
					};
					if (ImGui::Combo("Colour mode",
					                 reinterpret_cast<int *>(&colour_method),
					                 labels,
					                 4))
					{
						calculate_camera_colour_space();
						update_camera_primaries  = true;
						update_raw_white_balance = true;
						update_user_mat          = true;
					}
				}

				if (colour_method == colour_space_method::BW_neg)
				{
					const char *labels[4] = {
					  "All channels",
					  "Red channel",
					  "Green channel",
					  "Blue channel",
					};
					update_user_mat |= ImGui::Combo(
					  "BW source", reinterpret_cast<int *>(&bw_source), labels, 4);
				}

				if (update_user_mat)
				{
					switch (colour_method)
					{
						case colour_space_method::R_G_B_neg:
						{
							user_mat = lak::diagonal(lak::vec4f_t(lak::vec3f_t(-1.f), 1.f));
							user_mat.x.w = 1.f;
							user_mat.y.w = 1.f;
							user_mat.z.w = 1.f;
						}
						break;

						case colour_space_method::BW_neg:
						{
							lak::vec4f_t basis;
							switch (bw_source)
							{
								default:
									[[fallthrough]];
								case bw_source_channel::W_channel:
									basis = lak::vec4f_t(lak::vec3f_t(-1.f), 3.f);
									break;
								case bw_source_channel::R_channel:
									basis = lak::vec4f_t(-1.f, 0.f, 0.f, 1.f);
									break;
								case bw_source_channel::G_channel:
									basis = lak::vec4f_t(0.f, -1.f, 0.f, 1.f);
									break;
								case bw_source_channel::B_channel:
									basis = lak::vec4f_t(0.f, 0.f, -1.f, 1.f);
									break;
							}
							user_mat = lak::mat4f_t{
							  basis,
							  basis,
							  basis,
							  lak::vec4f_t(lak::vec3f_t(0.f), 1.f),
							};
						}
						break;

						default:
							user_mat = lak::diagonal(lak::vec4f_t(1.f));
							break;
					}

					image_viewport_state.compute_bindings
					  .template set_state_value<"user_matrix">(
					    lak::cobalt::from_lak(user_mat));
				}

				ImGui::Separator();

				if (colour_method == colour_space_method::IR_R_G)
				{
					ImGui::Text("Raw IR white point");

					if (!use_database_ir_balance || !used_ir_balance_from_db)
					{
						update_raw_white_balance |= ImGui::DragFloat(
						  "R##IR in", &ir_balance.ir_in.r, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
						update_raw_white_balance |= ImGui::DragFloat(
						  "G##IR in", &ir_balance.ir_in.g, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
						update_raw_white_balance |= ImGui::DragFloat(
						  "B##IR in", &ir_balance.ir_in.b, 0.0001f, 0.1f, 2.0f, "IR*%.3f");
					}
					else
					{
						ImGui::Text(
						  "IR in R: %.3f/%.3f", ir_balance.ir_in.r, ir_balance.ir_in.b);
						ImGui::Text(
						  "IR in G: %.3f/%.3f", ir_balance.ir_in.g, ir_balance.ir_in.b);
					}

					ImGui::Separator();

					ImGui::Text("Raw RGB white point");

					if (!use_database_aero_match || !used_aero_match_from_db)
					{
						update_raw_white_balance |=
						  ImGui::DragFloat("R##W",
						                   &ir_balance.aero_match.r,
						                   0.001f,
						                   0.001f,
						                   2.0f,
						                   "IR/%.3f");
						update_raw_white_balance |=
						  ImGui::DragFloat("G##W",
						                   &ir_balance.aero_match.g,
						                   0.001f,
						                   0.001f,
						                   2.0f,
						                   "R/%.3f");
						update_raw_white_balance |=
						  ImGui::DragFloat("B##W",
						                   &ir_balance.aero_match.b,
						                   0.001f,
						                   0.001f,
						                   2.0f,
						                   "G/%.3f");
					}
					else
					{
						ImGui::Text("R: IR/%.3f\nG: R/%.3f\nB: G/%.3f",
						            ir_balance.aero_match.r,
						            ir_balance.aero_match.g,
						            ir_balance.aero_match.b);
					}

					ImGui::Separator();
				}
				else if (colour_method == colour_space_method::R_G_B_neg)
				{
					if (auto base_colour =
					      lak::vec3f_t(-user_mat.x.x, -user_mat.y.y, -user_mat.z.z);
					    ImGui::DragFloat3(
					      "Base colour", &base_colour.x, 0.05f, 0.f, 100.f))
					{
						user_mat     = lak::diagonal(lak::vec4f_t(-base_colour, 1.f));
						user_mat.x.w = 1.f;
						user_mat.y.w = 1.f;
						user_mat.z.w = 1.f;
						image_viewport_state.compute_bindings
						  .template set_state_value<"user_matrix">(
						    lak::cobalt::from_lak(user_mat));
					}
					ImGui::Separator();
				}
				else if (colour_method == colour_space_method::R_G_B)
				{
					bool update_user = false;

					update_user |= ImGui::DragFloat4(
					  "R##user_matrix_x", &user_mat.x.x, 0.05f, -10.f, 10.f);
					update_user |= ImGui::DragFloat4(
					  "G##user_matrix_y", &user_mat.y.x, 0.05f, -10.f, 10.f);
					update_user |= ImGui::DragFloat4(
					  "B##user_matrix_z", &user_mat.z.x, 0.05f, -10.f, 10.f);
					update_user |= ImGui::DragFloat4(
					  "H##user_matrix_w", &user_mat.w.x, 0.05f, -10.f, 10.f);

					if (update_user)
						image_viewport_state.compute_bindings
						  .template set_state_value<"user_matrix">(
						    lak::cobalt::from_lak(user_mat));
					ImGui::Separator();
				}

#if 0
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

				ImGui::Separator();
#endif

				{
					update_raw_white_balance |= ImGui::DragFloat(
					  "Temperature",
					  &colour_temp,
					  10.f,
					  ((colour_method == colour_space_method::IR_R_G) ? 1000.f : 4000.f),
					  25000.f,
					  "%.0fK");

					if (ImPlot::BeginPlot("Temperature spectrum", ImVec2(-1, 250)))
					{
						DEFER(ImPlot::EndPlot());

						const int wavelength_min   = 200;
						const int wavelength_max   = 1000;
						const int wavelength_steps = 10;

						const auto peak_radiance = lak::blackbody_radiance(
						  std::min(std::max(lak::blackbody_peak_wavelength(colour_temp),
						                    double(wavelength_min)),
						           double(wavelength_max)),
						  colour_temp);

						ImPlot::SetupAxis(ImAxis_X1,
						                  "Wavelength (nm)",
						                  ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus |
						                    ImPlotAxisFlags_NoSideSwitch);
						ImPlot::SetupAxisLimits(ImAxis_X1,
						                        double(wavelength_min),
						                        double(wavelength_max),
						                        ImPlotCond_Always);

						ImPlot::SetupAxis(ImAxis_Y1,
						                  "Sun's radiance (W/sr/m^2/nm)",
						                  ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus |
						                    ImPlotAxisFlags_NoSideSwitch);
						ImPlot::SetupAxisLimits(
						  ImAxis_Y1, 0.0, peak_radiance, ImPlotCond_Always);

						ImPlot::SetupAxis(ImAxis_Y2,
						                  "Earth's irradiance (W/m^2/nm)",
						                  ImPlotAxisFlags_Lock | ImPlotAxisFlags_NoMenus |
						                    ImPlotAxisFlags_NoSideSwitch |
						                    ImPlotAxisFlags_Opposite);
						ImPlot::SetupAxisLimits(ImAxis_Y2,
						                        0.0,
						                        peak_radiance * lak::suns_steradian,
						                        ImPlotCond_Always);

						ImPlot::SetupLegend(ImPlotLocation_SouthWest);
						ImPlot::SetupFinish();

						// ---

						ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
						ImPlotGetter radiance_getter = [](int index,
						                                  void *data) -> ImPlotPoint
						{
							const auto *wnd = reinterpret_cast<const rye_window *>(data);
							const double x =
							  double((index * wavelength_steps) + wavelength_min);
							const double y = lak::blackbody_radiance(x, wnd->colour_temp);
							return {x, y};
						};

						ImPlot::PlotLineG("##blackbody",
						                  radiance_getter,
						                  this,
						                  (wavelength_max - wavelength_min) /
						                    wavelength_steps);
					}

					if (ImGui::Button("Reset to camera##raw white balance"))
					{
						colour_temp              = 5000.0f;
						update_raw_white_balance = true;
					}
					ImGui::Separator();
				}

				if (ImGui::DragFloat("Exposure", &exposure, 0.01f, 0.f, 1000.f))
				{
					image_viewport_state.compute_bindings
					  .template set_state_value<"exposure">(exposure);
				}

				if (ImGui::DragFloat("Contrast", &contrast, 0.1f, -1000.f, 1000.f))
				{
					image_viewport_state.compute_bindings
					  .template set_state_value<"contrast">(contrast);
				}

				if (ImGui::DragFloat("Lightness", &lightness, 0.1f, -1000.f, 1000.f))
				{
					image_viewport_state.compute_bindings
					  .template set_state_value<"lightness">(lightness);
				}

				if (ImGui::DragFloat("Saturation", &saturation, 0.1f, -1000.f, 1000.f))
				{
					image_viewport_state.compute_bindings
					  .template set_state_value<"saturation">(saturation);
				}

				// if (ImGui::DragFloat("Hue (degrees)", &hue, 1.f, -360.f, 360.f))
				// {
				// 	image_viewport_state.compute_bindings.template
				// set_state_value<"hue">(hue);
				// }

				ImGui::Separator();

				{
					update_camera_primaries |= lak::ChromaticityEdit(
					  "Camera",
					  &camera_primaries,
					  ImVec2(left_content_size.x, left_content_size.x));
					if (ImGui::Button("Reset to camera##camera primaries"))
					{
						calculate_camera_colour_space();
						update_camera_primaries = true;
					}
					lak::Text<
					  u8"r: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "g: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "b: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "w: (x:{:-+9.3} y:{:-+.3})">(camera_primaries.r.x,
					                               camera_primaries.r.y,
					                               camera_primaries.r.Y,
					                               camera_primaries.g.x,
					                               camera_primaries.g.y,
					                               camera_primaries.g.Y,
					                               camera_primaries.b.x,
					                               camera_primaries.b.y,
					                               camera_primaries.b.Y,
					                               camera_primaries.w.x,
					                               camera_primaries.w.y);
				}

				{
					update_scene_primaries |=
					  lak::ChromaticityEdit(
					    "Scene",
					    &scene_primaries,
					    ImVec2(left_content_size.x, left_content_size.x)) |
					  update_camera_primaries;
					if (ImGui::Button("Reset to camera##scene primaries"))
					{
						scene_primaries        = camera_primaries;
						update_scene_primaries = true;
					}
					if (update_scene_primaries) calculate_scene_colour_space();
					lak::Text<
					  u8"r: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "g: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "b: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "w: (x:{:-+9.3} y:{:-+.3})">(scene_primaries.r.x,
					                               scene_primaries.r.y,
					                               scene_primaries.r.Y,
					                               scene_primaries.g.x,
					                               scene_primaries.g.y,
					                               scene_primaries.g.Y,
					                               scene_primaries.b.x,
					                               scene_primaries.b.y,
					                               scene_primaries.b.Y,
					                               scene_primaries.w.x,
					                               scene_primaries.w.y);
				}

				ImGui::Separator();

				{
					bool update_output = lak::ChromaticityEdit(
					  "Output",
					  &display_primaries,
					  ImVec2(left_content_size.x, left_content_size.x));
					update_output |=
					  ImGui::DragFloat("Gamma", &display_gamma, 0.001f, 1.f, 3.f);
					if (ImGui::Button("sRGB"))
					{
						display_gamma     = 2.2f;
						display_primaries = lak::col::sRGB_primaries;
						update_output     = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Adobe RGB"))
					{
						display_gamma     = 1.f;
						display_primaries = lak::col::opRGB_primaries;
						update_output     = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Camera (RAW)"))
					{
						display_gamma = 1.f;
						auto prim     = lak::transpose(cam_to_XYZ);
						display_primaries.r =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.x));
						display_primaries.g =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.y));
						display_primaries.b =
						  lak::col::cie::to_xyY(lak::col::cie::XYZ::from_vec(prim.z));
						display_primaries.regenerate_w();
						display_primaries.regenerate_Y();
						update_output = true;
					}
					ImGui::SameLine();
					if (ImGui::Button("Camera (current)"))
					{
						display_gamma     = 1.f;
						display_primaries = camera_primaries;
						update_output     = true;
					}
					if (update_output) update_display_colour_space();
					lak::Text<
					  u8"r: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "g: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "b: (x:{:-+9.3} y:{:-+9.3} Y:{:-+.3})\n"
					  "w: (x:{:-+9.3} y:{:-+.3})">(display_primaries.r.x,
					                               display_primaries.r.y,
					                               display_primaries.r.Y,
					                               display_primaries.g.x,
					                               display_primaries.g.y,
					                               display_primaries.g.Y,
					                               display_primaries.b.x,
					                               display_primaries.b.y,
					                               display_primaries.b.Y,
					                               display_primaries.w.x,
					                               display_primaries.w.y);
				}

				if (update_camera_primaries | update_scene_primaries)
					update_camera_colour_space();
				if (update_raw_white_balance | update_scene_primaries)
					update_camera_white_balance();

				ImGui::Separator();

				if (image_process) ImGui::Text("Processing...");

				ImGui::EndChild();

				ImGui::SameLine();

				ImGui::BeginChild("ImgRight",
				                  {right_size, -1},
				                  true,
				                  ImGuiWindowFlags_NoSavedSettings);
#if 0
				LAK_TREE_NODE("RAW") { rye::image_view(lrawtex, &lrawtex_size); }
				LAK_TREE_NODE("PROC RAW")
				{
					rye::image_view(lrprocessedtex, &lrawptex_size);
				}
				LAK_TREE_NODE("IR BALANCE WAVEFORM")
				{
					rye::image_view(lrwavetex, &lrawwtex_size);
				}
				LAK_TREE_NODE("WHITE BALANCE WAVEFORM")
				{
					rye::image_view(lrwave2tex, &lraww2tex_size);
				}
				LAK_TREE_NODE("DEBAYER")
				{
					rye::image_view(lrdebayertex, &lrawdtex_size);
				}
#endif
				// LAK_TREE_NODE("sRGB")
				if (finaltex)
				{
					ImGui::PushID("sRGB");
					DEFER(ImGui::PopID());

					if (!image_viewport) [[unlikely]]
						image_viewport.emplace(lak::ImTextureColourFormat::RGB,
						                       lak::ImTextureChannelFormat::U8);

					ImGui::DragFloat("Scale", &lrawsrgbtex_size, 0.01f, 0.1f, 10.0f);
					ImGui::Separator();

					ImGui::BeginChild("Image View",
					                  ImVec2(0, 0),
					                  false,
					                  ImGuiWindowFlags_NoSavedSettings |
					                    ImGuiWindowFlags_AlwaysVerticalScrollbar |
					                    ImGuiWindowFlags_AlwaysHorizontalScrollbar);
					DEFER(ImGui::EndChild());

					const auto sz = lak::TextureSize(finaltex);
					auto vpd =
					  *lak::BeginViewport(image_viewport.get(),
					                      ImVec2(lrawsrgbtex_size * (float)sz.x,
					                             lrawsrgbtex_size * (float)sz.y))
					     .template get<ImGui::ImplCoViewportDetails>();
					DEFER(lak::EndViewport(image_viewport.get()));

					if (vpd.passes->empty())
					{
						// :FIXME: for compatibility reasons compute passes cannot be
						// mixed with regular passes!
						auto *compute_pass = vpd.append_pass();
						compute_pass->BindFrameBuffer(nullptr);

						auto clear_pass = vpd.append_pass();
						clear_pass->SetAttachmentClearData(
						  cobalt::graphics::IFrameBuffer::AttachmentType::Color,
						  0,
						  cobalt::graphics::V4Float32{0., 0.3125f, 0.3125f, 1.0f});
						clear_pass->SetAttachmentClearData(
						  cobalt::graphics::IFrameBuffer::AttachmentType::Depth,
						  0,
						  cobalt::graphics::V4Float32{1.f, 1.f, 1.f, 1.f});

						auto *display_pass = vpd.append_pass();
						image_viewport_state =
						  rye_gpu_image_process_state::make(
						    window(),
						    compute_pass,
						    display_pass,
						    ImGui::ImplGetCobaltTexture(finaltex.get().GetTexID()))
						    .UNWRAP();

						// if (image_viewport_state.XYZ_to_display !=
						//     cobalt::graphics::StateValueId::Null)
						// {
						// 	auto &col = lraw->imgdata.color;
						// 	auto mat  = lak::mat4f_t{
						//     lak::vec4f_t(col.rgb_cam[0][0],
						//                  col.rgb_cam[0][1],
						//                  col.rgb_cam[0][2],
						//                  col.rgb_cam[0][3]),
						//     lak::vec4f_t(col.rgb_cam[1][0],
						//                  col.rgb_cam[1][1],
						//                  col.rgb_cam[1][2],
						//                  col.rgb_cam[1][3]),
						//     lak::vec4f_t(col.rgb_cam[2][0],
						//                  col.rgb_cam[2][1],
						//                  col.rgb_cam[2][2],
						//                  col.rgb_cam[2][3]),
						//     lak::vec4f_t(0.f, 0.f, 0.f, 1.f),
						//   };
						// 	image_viewport_state.state_group_node->SetStateValue(
						// 	  image_viewport_state.XYZ_to_display,
						// 	  lak::cobalt::from_lak(mat));
						// }

						update_camera_colour_space();
						update_display_colour_space();
						update_camera_white_balance();

						image_viewport_state.compute_bindings
						  .template set_state_value<"user_matrix">(
						    lak::cobalt::from_lak(user_mat));

						image_viewport_state.compute_bindings
						  .template set_state_value<"exposure">(exposure);

						image_viewport_state.compute_bindings
						  .template set_state_value<"contrast">(contrast);

						image_viewport_state.compute_bindings
						  .template set_state_value<"lightness">(lightness);

						image_viewport_state.compute_bindings
						  .template set_state_value<"saturation">(saturation);

						image_viewport_state.compute_bindings
						  .template set_state_value<"hue">(hue);

						// image_viewport_state.compute_bindings
						//   .template
						//   set_state_value<"camera_white_to_display_white_XYZ">(
						//     lak::cobalt::from_lak(display_primaries.w_XYZ().to_vec()));

						// if (image_viewport_state.temperature !=
						//     cobalt::graphics::StateValueId::Null)
						// {
						// 	auto wb_wv = rye::relative_blackbody(colour_temp);
						// 	const lak::vec3f_t temp_sensitivity{
						// 	  wb_wv(850.0), wb_wv(600.0), wb_wv(525.0)};
						// 	const lak::vec3f_t temp_balance =
						// 	  rye::white_balance(temp_sensitivity);
						// 	image_viewport_state.state_group_node->SetStateValue(
						// 	  image_viewport_state.temperature,
						// 	  lak::cobalt::from_lak(temp_balance));
						// }
					}
					// rye_image_view(lrsrgbtex, &lrawsrgbtex_size);
				}
				ImGui::EndChild();
			}
		}
	}

	virtual void loop(uint64_t counter_delta) override final
	{
		const float frame_time =
		  (float)counter_delta / lak::performance_frequency();

		if (ImGui::BeginMenuBar())
		{
			menu_bar(frame_time);
			ImGui::EndMenuBar();
		}

		main_region(frame_time);
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
