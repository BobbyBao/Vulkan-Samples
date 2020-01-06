/* Copyright (c) 2019, Sascha Willems
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 the "License";
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Demonstrates the use of dynamic uniform buffers.
 *
 * Instead of using one uniform buffer per-object, this example allocates one big uniform buffer
 * with respect to the alignment reported by the device via minUniformBufferOffsetAlignment that
 * contains all matrices for the objects in the scene.
 *
 * The used descriptor type vk::DescriptorType::eUniformBufferDynamic then allows to set a dynamic
 * offset used to pass data from the single uniform buffer to the connected shader binding point.
 */

#include "dynamic_uniform_buffers.h"

DynamicUniformBuffers::DynamicUniformBuffers()
{
	title = "Dynamic uniform buffers";
}

DynamicUniformBuffers ::~DynamicUniformBuffers()
{
	if (device)
	{
		if (ubo_data_dynamic.model)
		{
			aligned_free(ubo_data_dynamic.model);
		}

		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class
		get_device().get_handle().destroy(pipeline);

		get_device().get_handle().destroy(pipeline_layout);
		get_device().get_handle().destroy(descriptor_set_layout);
	}
}

// Wrapper functions for aligned memory allocation
// There is currently no standard for this in C++ that works across all platforms and vendors, so we abstract this
void *DynamicUniformBuffers::aligned_alloc(size_t size, size_t alignment)
{
	void *data = nullptr;
#if defined(_MSC_VER) || defined(__MINGW32__)
	data = _aligned_malloc(size, alignment);
#else
	int res = posix_memalign(&data, alignment, size);
	if (res != 0)
		data = nullptr;
#endif
	return data;
}

void DynamicUniformBuffers::aligned_free(void *data)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
	_aligned_free(data);
#else
	free(data);
#endif
}

void DynamicUniformBuffers::build_command_buffers()
{
	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	vk::ClearValue clear_values[2];
	clear_values[0].color        = default_clear_color;
	clear_values[1].depthStencil = {0.0f, 0};

	vk::RenderPassBeginInfo render_pass_begin_info  = vkb::initializers::render_pass_begin_info();
	render_pass_begin_info.renderPass               = render_pass;
	render_pass_begin_info.renderArea.offset.x      = 0;
	render_pass_begin_info.renderArea.offset.y      = 0;
	render_pass_begin_info.renderArea.extent.width  = width;
	render_pass_begin_info.renderArea.extent.height = height;
	render_pass_begin_info.clearValueCount          = 2;
	render_pass_begin_info.pClearValues             = clear_values;

	for (int32_t i = 0; i < draw_cmd_buffers.size(); ++i)
	{
		render_pass_begin_info.framebuffer = framebuffers[i];
		auto &draw_cmd_buffer              = draw_cmd_buffers[i];

		draw_cmd_buffer.begin(command_buffer_begin_info);

		draw_cmd_buffer.beginRenderPass(&render_pass_begin_info, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
		draw_cmd_buffer.setViewport(0, viewport);

		vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
		draw_cmd_buffer.setScissor(0, scissor);

		draw_cmd_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

		vk::DeviceSize offsets[1] = {0};
		draw_cmd_buffer.bindVertexBuffers(0, 1, vertex_buffer->get(), offsets);
		draw_cmd_buffer.bindIndexBuffer(index_buffer->get_handle(), 0, vk::IndexType::eUint32);

		// Render multiple objects using different model matrices by dynamically offsetting into one uniform buffer
		for (uint32_t j = 0; j < OBJECT_INSTANCES; j++)
		{
			// One dynamic offset per dynamic descriptor to offset into the ubo containing all model matrices
			uint32_t dynamic_offset = j * static_cast<uint32_t>(dynamic_alignment);
			// Bind the descriptor set for rendering a mesh using the dynamic offset
			draw_cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptor_set, dynamic_offset);

			draw_cmd_buffer.drawIndexed(index_count, 1, 0, 0, 0);
		}

		draw_ui(draw_cmd_buffers[i]);

		draw_cmd_buffers[i].endRenderPass();

		draw_cmd_buffers[i].end();
	}
}

void DynamicUniformBuffers::draw()
{
	ApiVulkanSample::prepare_frame();

	// Command buffer to be sumitted to the queue
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];

	// Submit to queue
	queue.submit(submit_info, nullptr);

	ApiVulkanSample::submit_frame();
}

void DynamicUniformBuffers::generate_cube()
{
	// Setup vertices indices for a colored cube
	std::vector<Vertex> vertices = {
	    {{-1.0f, -1.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
	    {{1.0f, -1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
	    {{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
	    {{-1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
	    {{1.0f, -1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
	    {{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, 0.0f}},
	};

	std::vector<uint32_t> indices = {
	    0,
	    1,
	    2,
	    2,
	    3,
	    0,
	    1,
	    5,
	    6,
	    6,
	    2,
	    1,
	    7,
	    6,
	    5,
	    5,
	    4,
	    7,
	    4,
	    0,
	    3,
	    3,
	    7,
	    4,
	    4,
	    5,
	    1,
	    1,
	    0,
	    4,
	    3,
	    2,
	    6,
	    6,
	    7,
	    3,
	};

	index_count = static_cast<uint32_t>(indices.size());

	auto vertex_buffer_size = vertices.size() * sizeof(Vertex);
	auto index_buffer_size  = indices.size() * sizeof(uint32_t);

	// Create buffers
	// For the sake of simplicity we won't stage the vertex data to the gpu memory
	// Vertex buffer
	vertex_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                    vertex_buffer_size,
	                                                    vk::BufferUsageFlagBits::eVertexBuffer,
	                                                    vma::MemoryUsage::eGpuToCpu);
	vertex_buffer->update(vertices.data(), vertex_buffer_size);

	index_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                   index_buffer_size,
	                                                   vk::BufferUsageFlagBits::eIndexBuffer,
	                                                   vma::MemoryUsage::eGpuToCpu);
	index_buffer->update(indices.data(), index_buffer_size);
}

void DynamicUniformBuffers::setup_descriptor_pool()
{
	// Example uses one ubo and one image sampler
	std::vector<vk::DescriptorPoolSize> pool_sizes =
	    {
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 1),
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBufferDynamic, 1),
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 1)};

	vk::DescriptorPoolCreateInfo descriptor_pool_create_info =
	    vkb::initializers::descriptor_pool_create_info(
	        static_cast<uint32_t>(pool_sizes.size()),
	        pool_sizes.data(),
	        2);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);
}

void DynamicUniformBuffers::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings =
	    {
	        vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0),
	        vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBufferDynamic, vk::ShaderStageFlagBits::eVertex, 1),
	        vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2)};

	vk::DescriptorSetLayoutCreateInfo descriptor_layout =
	    vkb::initializers::descriptor_set_layout_create_info(
	        set_layout_bindings.data(),
	        static_cast<uint32_t>(set_layout_bindings.size()));

	descriptor_set_layout = get_device().get_handle().createDescriptorSetLayout(descriptor_layout);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info =
	    vkb::initializers::pipeline_layout_create_info(
	        &descriptor_set_layout,
	        1);

	pipeline_layout = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);
}

void DynamicUniformBuffers::setup_descriptor_set()
{
	vk::DescriptorSetAllocateInfo alloc_info =
	    vkb::initializers::descriptor_set_allocate_info(
	        descriptor_pool,
	        &descriptor_set_layout,
	        1);

	descriptor_set = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo            view_buffer_descriptor    = create_descriptor(*uniform_buffers.view);
	vk::DescriptorBufferInfo            dynamic_buffer_descriptor = create_descriptor(*uniform_buffers.dynamic);
	std::vector<vk::WriteDescriptorSet> write_descriptor_sets     = {
        // Binding 0 : Projection/View matrix uniform buffer
        vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eUniformBuffer, 0, &view_buffer_descriptor),
        // Binding 1 : Instance matrix as dynamic uniform buffer
        vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eUniformBufferDynamic, 1, &dynamic_buffer_descriptor),
    };

	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void DynamicUniformBuffers::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info();

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(vk::CullModeFlagBits::eNone);

	vk::PipelineColorBlendAttachmentState blend_attachment_state =
	    vkb::initializers::pipeline_color_blend_attachment_state();

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(
	        1,
	        &blend_attachment_state);

	// Note: Using Reversed depth-buffer for increased precision, so Greater depth values are kept
	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info(
	        VK_TRUE,
	        VK_TRUE,
	        vk::CompareOp::eGreater);

	vk::PipelineViewportStateCreateInfo viewport_state =
	    vkb::initializers::pipeline_viewport_state_create_info();

	vk::PipelineMultisampleStateCreateInfo multisample_state =
	    vkb::initializers::pipeline_multisample_state_create_info();

	std::vector<vk::DynamicState> dynamic_state_enables = {
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eScissor,
	};
	vk::PipelineDynamicStateCreateInfo dynamic_state =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	// Load shaders
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages;

	shader_stages[0] = load_shader("dynamic_uniform_buffers/base.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("dynamic_uniform_buffers/base.frag", vk::ShaderStageFlagBits::eFragment);

	// Vertex bindings and attributes
	const std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex)),
	};
	const std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),          // Location 0 : Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, color)),        // Location 1 : Color
	};
	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(pipeline_layout, render_pass);

	pipeline_create_info.pVertexInputState   = &vertex_input_state;
	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;
	pipeline_create_info.stageCount          = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages             = shader_stages.data();

	pipeline = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

// Prepare and initialize uniform buffer containing shader uniforms
void DynamicUniformBuffers::prepare_uniform_buffers()
{
	// Allocate data for the dynamic uniform buffer object
	// We allocate this manually as the alignment of the offset differs between GPUs

	// Calculate required alignment based on minimum device offset alignment
	size_t min_ubo_alignment = get_device().get_properties().limits.minUniformBufferOffsetAlignment;
	dynamic_alignment        = sizeof(glm::mat4);
	if (min_ubo_alignment > 0)
	{
		dynamic_alignment = (dynamic_alignment + min_ubo_alignment - 1) & ~(min_ubo_alignment - 1);
	}

	size_t buffer_size = OBJECT_INSTANCES * dynamic_alignment;

	ubo_data_dynamic.model = (glm::mat4 *) aligned_alloc(buffer_size, dynamic_alignment);
	assert(ubo_data_dynamic.model);

	std::cout << "minUniformBufferOffsetAlignment = " << min_ubo_alignment << std::endl;
	std::cout << "dynamicAlignment = " << dynamic_alignment << std::endl;

	// Vertex shader uniform buffer block

	// Static shared uniform buffer object with projection and view matrix
	uniform_buffers.view = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                           sizeof(ubo_vs),
	                                                           vk::BufferUsageFlagBits::eUniformBuffer,
	                                                           vma::MemoryUsage::eCpuToGpu);

	uniform_buffers.dynamic = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                              buffer_size,
	                                                              vk::BufferUsageFlagBits::eUniformBuffer,
	                                                              vma::MemoryUsage::eCpuToGpu);

	// Prepare per-object matrices with offsets and random rotations
	std::default_random_engine      rnd_engine(is_benchmark_mode() ? 0 : (unsigned) time(nullptr));
	std::normal_distribution<float> rnd_dist(-1.0f, 1.0f);
	for (uint32_t i = 0; i < OBJECT_INSTANCES; i++)
	{
		rotations[i]       = glm::vec3(rnd_dist(rnd_engine), rnd_dist(rnd_engine), rnd_dist(rnd_engine)) * 2.0f * glm::pi<float>();
		rotation_speeds[i] = glm::vec3(rnd_dist(rnd_engine), rnd_dist(rnd_engine), rnd_dist(rnd_engine));
	}

	update_uniform_buffers();
	update_dynamic_uniform_buffer(0.0f, true);
}

void DynamicUniformBuffers::update_uniform_buffers()
{
	// Fixed ubo with projection and view matrices
	ubo_vs.projection = camera.matrices.perspective;
	ubo_vs.view       = camera.matrices.view;

	uniform_buffers.view->convert_and_update(ubo_vs);
}

void DynamicUniformBuffers::update_dynamic_uniform_buffer(float delta_time, bool force)
{
	// Update at max. 60 fps
	animation_timer += delta_time;
	if ((animation_timer + 0.0025 < (1.0f / 60.0f)) && (!force))
	{
		return;
	}

	// Dynamic ubo with per-object model matrices indexed by offsets in the command buffer
	uint32_t  dim = static_cast<uint32_t>(pow(OBJECT_INSTANCES, (1.0f / 3.0f)));
	glm::vec3 offset(5.0f);

	for (uint32_t x = 0; x < dim; x++)
	{
		for (uint32_t y = 0; y < dim; y++)
		{
			for (uint32_t z = 0; z < dim; z++)
			{
				uint32_t index = x * dim * dim + y * dim + z;

				// Aligned offset
				glm::mat4 *model_mat = (glm::mat4 *) (((uint64_t) ubo_data_dynamic.model + (index * dynamic_alignment)));

				// Update rotations
				rotations[index] += animation_timer * rotation_speeds[index];

				// Update matrices
				glm::vec3 pos = glm::vec3(-((dim * offset.x) / 2.0f) + offset.x / 2.0f + x * offset.x, -((dim * offset.y) / 2.0f) + offset.y / 2.0f + y * offset.y, -((dim * offset.z) / 2.0f) + offset.z / 2.0f + z * offset.z);
				*model_mat    = glm::translate(glm::mat4(1.0f), pos);
				*model_mat    = glm::rotate(*model_mat, rotations[index].x, glm::vec3(1.0f, 1.0f, 0.0f));
				*model_mat    = glm::rotate(*model_mat, rotations[index].y, glm::vec3(0.0f, 1.0f, 0.0f));
				*model_mat    = glm::rotate(*model_mat, rotations[index].z, glm::vec3(0.0f, 0.0f, 1.0f));
			}
		}
	}

	animation_timer = 0.0f;

	uniform_buffers.dynamic->update(ubo_data_dynamic.model, uniform_buffers.dynamic->get_size());

	// Flush to make changes visible to the device
	uniform_buffers.dynamic->flush();
}

bool DynamicUniformBuffers::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	camera.type = vkb::CameraType::LookAt;
	camera.set_position(glm::vec3(0.0f, 0.0f, -30.0f));
	camera.set_rotation(glm::vec3(0.0f));

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.set_perspective(60.0f, (float) width / (float) height, 256.0f, 0.1f);

	generate_cube();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_set();
	build_command_buffers();
	prepared = true;
	return true;
}

void DynamicUniformBuffers::resize(const uint32_t width, const uint32_t height)
{
	ApiVulkanSample::resize(width, height);
	update_uniform_buffers();
}

void DynamicUniformBuffers::render(float delta_time)
{
	if (!prepared)
	{
		return;
	}
	draw();
	if (!paused)
	{
		update_dynamic_uniform_buffer(delta_time);
	}
	if (camera.updated)
	{
		update_uniform_buffers();
	}
}

std::unique_ptr<vkb::Application> create_dynamic_uniform_buffers()
{
	return std::make_unique<DynamicUniformBuffers>();
}
