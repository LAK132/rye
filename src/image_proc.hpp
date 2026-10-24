
#include <lak/system/cobalt/math.hpp>
#include <lak/system/cobalt/program.hpp>
#include <lak/system/cobalt/result.hpp>
#include <lak/system/cobalt/state.hpp>
#include <lak/system/windowing/window.hpp>

struct rye_gpu_image_process_state
{
	cobalt::graphics::IRenderPassNode::unique_ptr pass_node;
	cobalt::graphics::IRenderPassNode::unique_ptr display_pass_node;
	cobalt::graphics::IShaderProgram::unique_ptr program;
	cobalt::graphics::IShaderProgram::unique_ptr display_program;
	cobalt::graphics::IVertexBuffer::unique_ptr vertex_buffer;
	cobalt::graphics::IRenderableNode::unique_ptr renderable_node;

	cobalt::graphics::ITexelArray::unique_ptr image_buffer;
	cobalt::graphics::ITexelArrayOutput::unique_ptr image_capture;

	cobalt::graphics::ITexelArray::unique_ptr display_image_buffer;
	// cobalt::graphics::ITextureBuffer2D::unique_ptr display_image_buffer;

	// OpenGL always requires a sampler when dealing with textures
	cobalt::graphics::ITextureSampler2D::unique_ptr sampler;

	lak::cobalt::program_state<
	  lak::type_pack<
	    lak::cobalt::state_value_binding<cobalt::graphics::V2UInt32,
	                                     "image_size">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "camera_to_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "scene_to_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "XYZ_to_scene">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "XYZ_to_display">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "camera_XYZ_to_scene_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "user_adaptation_XYZ">,
	    lak::cobalt::state_value_binding<cobalt::graphics::M3Float32,
	                                     "scene_XYZ_to_display_XYZ">,
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
	    lak::cobalt::texture_binding<cobalt::graphics::ITextureBuffer2D, "tex">
	    // ,lak::cobalt::texture_binding<cobalt::graphics::ITextureBuffer2D,
	    //                              "display_image_buffer">
	    >>
	  bindings;

	lak::cobalt::program_state<lak::type_pack<
	  lak::cobalt::state_value_binding<cobalt::graphics::M4Float32, "view_proj">,
	  lak::cobalt::state_value_binding<cobalt::graphics::V2UInt32, "image_size">,
	  lak::cobalt::state_value_binding<cobalt::graphics::V4Float32, "image_uv">>>
	  display_bindings;

	rye_gpu_image_process_state() = default;
	rye_gpu_image_process_state(rye_gpu_image_process_state &&other);
	rye_gpu_image_process_state &operator=(rye_gpu_image_process_state &&other);
	rye_gpu_image_process_state(const rye_gpu_image_process_state &) = delete;
	rye_gpu_image_process_state &operator=(const rye_gpu_image_process_state &) =
	  delete;

	inline ~rye_gpu_image_process_state() { clear(); }

	void clear();

	static lak::result<rye_gpu_image_process_state, lak::u8string> make(
	  lak::window &wnd, cobalt::graphics::ITextureBuffer2D *source_texture);
};
