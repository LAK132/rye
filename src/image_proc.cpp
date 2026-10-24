#include "image_proc.hpp"

rye_gpu_image_process_state::rye_gpu_image_process_state(
  rye_gpu_image_process_state &&other)
: pass_node(lak::move(other.pass_node)),
  display_pass_node(lak::move(other.display_pass_node)),
  program(lak::move(other.program)),
  display_program(lak::move(other.display_program)),
  vertex_buffer(lak::move(other.vertex_buffer)),
  renderable_node(lak::move(other.renderable_node)),
  image_buffer(lak::move(other.image_buffer)),
  image_capture(lak::move(other.image_capture)),
  display_image_buffer(lak::move(other.display_image_buffer)),
  sampler(lak::move(other.sampler)),
  bindings(lak::move(other.bindings)),
  display_bindings(lak::move(other.display_bindings))
{
}

rye_gpu_image_process_state &rye_gpu_image_process_state::operator=(
  rye_gpu_image_process_state &&other)
{
	lak::swap(pass_node, other.pass_node);
	lak::swap(display_pass_node, other.display_pass_node);
	lak::swap(program, other.program);
	lak::swap(display_program, other.display_program);
	lak::swap(vertex_buffer, other.vertex_buffer);
	lak::swap(renderable_node, other.renderable_node);
	lak::swap(image_buffer, other.image_buffer);
	lak::swap(image_capture, other.image_capture);
	lak::swap(display_image_buffer, other.display_image_buffer);
	lak::swap(sampler, other.sampler);
	lak::swap(bindings, other.bindings);
	lak::swap(display_bindings, other.display_bindings);
	return *this;
}

void rye_gpu_image_process_state::clear()
{
	if (pass_node)
	{
		pass_node->RemoveAllChildNodes();
		pass_node.reset();
	}
	if (bindings.program_node)
	{
		bindings.program_node->RemoveAllChildNodes();
		bindings.program_node.reset();
	}
	if (bindings.state_group_node)
	{
		bindings.state_group_node->RemoveAllChildNodes();
		bindings.state_group_node.reset();
	}
	program.reset();

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

	if (image_buffer && image_capture)
		image_buffer->RemoveOutputCaptureTarget(image_capture.get());
	image_capture.reset();
	image_buffer.reset();

	display_image_buffer.reset();

	renderable_node.reset();
	vertex_buffer.reset();
}

lak::result<rye_gpu_image_process_state, lak::u8string>
rye_gpu_image_process_state::make(
  lak::window &wnd, cobalt::graphics::ITextureBuffer2D *source_texture)
{
	const auto &cgx = lak::cobalt_graphics_context(wnd.handle()).UNWRAP();
	auto *rd        = cgx.renderer.get();

	rye_gpu_image_process_state state;

	state.pass_node = rd->CreateRenderPassNode();

	state.display_pass_node = rd->CreateRenderPassNode();

	constexpr char _vs[] = {
#embed "image_proc_vert.hlsl"
	};
	const auto vs = lak::astring_view(_vs);

	constexpr char _fs[] = {
#embed "image_proc_frag.hlsl"
	};
	const auto fs = lak::astring_view(_fs);

	constexpr char _cs[] = {
#embed "image_proc_comp.hlsl"
	};
	const auto cs = lak::astring_view(_cs);

	state.program = rd->CreateShaderProgram();
	if (!state.program->LoadShaderStage(
	      cobalt::graphics::IShaderProgram::ShaderStage::Compute,
	      lak::cobalt::shader_source_hlsl(cs)))
	{
		ERROR("Loading compute shader stage failed");
		return lak::err_t{u8"Loading compute shader stage failed"_str};
	}
	if (!state.program->CompileProgram())
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

	state.bindings = state.bindings.make(rd, state.program.get()).UNWRAP();
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

	state.image_capture = rd->CreateTexelArrayOutput();
	if (!state.image_capture)
		return lak::err_t{u8"Failed to create texel array output"_str};
	state.image_capture->SetDetachAfterCapture(true);
	state.image_capture->SetArrayCaptureRegion(size_t(image_size.X()) *
	                                           size_t(image_size.Y()));

	// state.display_image_buffer = rd->CreateTextureBuffer2D();
	// state.display_image_buffer->SetTextureFormat(
	//   cobalt::graphics::ITextureBuffer::ImageFormat::RGBA,
	//   cobalt::graphics::ITextureBuffer::DataFormat::Float32);
	// state.display_image_buffer->SetTextureDimensions(
	//   {uint32_t(image_size.X()), uint32_t(image_size.Y())});
	// state.display_image_buffer->SetUsageFlags(
	//   cobalt::graphics::ITextureBuffer::UsageFlags::ShaderInput
	//   // | cobalt::graphics::ITextureBuffer::UsageFlags::ShaderOutput
	// );
	// if (!state.display_image_buffer->AllocateMemory())
	// 	return lak::err_t{u8"Failed to allocate display image"_str};

	state.display_image_buffer = rd->CreateTexelArray();
	state.display_image_buffer->SetBufferLayout(
	  cobalt::graphics::ITexelArray::ImageFormat::RGBA,
	  cobalt::graphics::ITexelArray::DataFormat::Float32,
	  size_t(image_size.X()) * size_t(image_size.Y()));
	state.display_image_buffer->SetUsageFlags(
	  cobalt::graphics::ITexelArray::UsageFlags::ShaderInput |
	  cobalt::graphics::ITexelArray::UsageFlags::ShaderOutput |
	  cobalt::graphics::ITexelArray::UsageFlags::TransferSource);
	if (!state.display_image_buffer->AllocateMemory())
		return lak::err_t{u8"Failed to allocate processed image"_str};

	const auto output = state.program->GetResourceArrayId("image_buffer");
	if (output == cobalt::graphics::ResourceArrayId::Null)
		return lak::err_t{u8"Failed to get output image binding"_str};
	const auto display_output =
	  state.program->GetResourceArrayId("display_image_buffer");
	if (display_output == cobalt::graphics::ResourceArrayId::Null)
		return lak::err_t{u8"Failed to get output display image binding"_str};
	const auto display_input =
	  state.display_program->GetResourceArrayId("display_image_buffer");
	if (display_input == cobalt::graphics::ResourceArrayId::Null)
		return lak::err_t{u8"Failed to get input display image binding"_str};

	state.bindings.state_group_node->BindResourceArray(output,
	                                                   state.image_buffer.get());
	state.bindings.state_group_node->BindResourceArray(
	  display_output, state.display_image_buffer.get());
	state.display_bindings.state_group_node->BindResourceArray(
	  display_input, state.display_image_buffer.get());

	BOUNDS_ASSERT(state.bindings.template set_texture<"tex">(
	  source_texture, state.sampler.get()));
	BOUNDS_ASSERT(
	  state.bindings.template set_state_value<"image_size">(image_size));
	BOUNDS_ASSERT(state.display_bindings.template set_state_value<"view_proj">(
	  lak::cobalt::from_lak(lak::mat4f_t{
	    lak::vec4f_t{2.f, 0.f, 0.f, -1.f},
	    lak::vec4f_t{0.f, -2.f, 0.f, 1.f} *
	      // :HACK: Cobalt cannot correct OpenGL's coordinate system when the
	      // framebuffer never leaves the GPU
	      (cgx.api_family == cobalt::graphics::IRendererPlugin::ApiFamily::OpenGL
	         ? -1.f
	         : 1.f),
	    lak::vec4f_t{0.f, 0.f, 1.f, 0.f},
	    lak::vec4f_t{0.f, 0.f, 0.f, 1.f},
	  })));
	BOUNDS_ASSERT(
	  state.display_bindings.template set_state_value<"image_size">(image_size));
	state.display_bindings.template set_state_value<"image_uv">(
	  lak::cobalt::from_lak(lak::vec4f_t(0.f, 0.f, 1.f, 1.f)));

	state.bindings.state_group_node->SetComputeTask(cobalt::graphics::V3UInt32(
	  (image_size.X() + 15U) / 16U, (image_size.Y() + 15U) / 16U, 1U));
	state.pass_node->AddChildNode(state.bindings.program_node.get());
	state.display_pass_node->AddChildNode(
	  state.display_bindings.program_node.get());

	state.display_bindings.state_group_node->SetPolygonFillMode(
	  cobalt::graphics::IStateGroupNode::PolygonFillMode::Solid);
	state.display_bindings.state_group_node->SetDepthTestEnabled(true);
	state.display_bindings.state_group_node->SetDepthWriteEnabled(true);

	lak::array<cobalt::graphics::V2Float32> verts({
	  {0.0f, 0.0f},
	  {1.0f, 0.0f},
	  {0.0f, 1.0f},
	  {0.0f, 1.0f},
	  {1.0f, 1.0f},
	  {1.0f, 0.0f},
	});

	cobalt::graphics::VertexAttribute<cobalt::graphics::V2Float32>
	  positions_attribute(
	    verts.size(),
	    cobalt::graphics::IVertexAttribute::PerformanceHint::WriteNever |
	      cobalt::graphics::IVertexAttribute::PerformanceHint::ReadNever,
	    cobalt::graphics::IVertexAttribute::PerformanceHint::WriteNever |
	      cobalt::graphics::IVertexAttribute::PerformanceHint::ReadOften);
	cobalt::graphics::VertexAttribute<cobalt::graphics::V2Float32>
	  texCoords_attribute(
	    verts.size(),
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

	if (!positions_attribute.SetInitialData(verts.data(), verts.size()))
	{
		ERROR("Failed to set initial positions data");
		return lak::err_t{u8"Failed to set initial positions data"_str};
	}
	if (!texCoords_attribute.SetInitialData(verts.data(), verts.size()))
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

	state.bindings.template set_state_value<"camera_to_XYZ">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"scene_to_XYZ">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"XYZ_to_scene">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"XYZ_to_display">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"camera_XYZ_to_scene_XYZ">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"user_adaptation_XYZ">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"scene_XYZ_to_display_XYZ">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"user_matrix">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec4f_t(1.f))));
	state.bindings.template set_state_value<"scene_white_XYZ">(
	  lak::cobalt::from_lak(lak::vec3f_t(1.f / 3.f)));
	state.bindings.template set_state_value<"exposure">(0.f);
	state.bindings.template set_state_value<"contrast">(0.f);
	state.bindings.template set_state_value<"lightness">(0.f);
	state.bindings.template set_state_value<"saturation">(0.f);
	state.bindings.template set_state_value<"hue">(0.f);
	state.bindings.template set_state_value<"raw_white_balance">(
	  lak::cobalt::from_lak(lak::diagonal(lak::vec3f_t(1.f))));
	state.bindings.template set_state_value<"display_gamma">(1.f);

	return lak::move_ok(state);
}