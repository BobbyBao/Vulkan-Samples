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
  * Compute shader N-body simulation using two passes and shared compute shader memory
  */

#include "compute_nbody.h"

ComputeNBody::ComputeNBody()
{
	title       = "Compute shader N-body system";
	camera.type = vkb::CameraType::LookAt;

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.set_perspective(60.0f, (float) width / (float) height, 512.0f, 0.1f);
	camera.set_rotation(glm::vec3(-26.0f, 75.0f, 0.0f));
	camera.set_translation(glm::vec3(0.0f, 0.0f, -14.0f));
	camera.translation_speed = 2.5f;
}

ComputeNBody::~ComputeNBody()
{
	if (device)
	{
		// Graphics
		graphics.uniform_buffer.reset();
		get_device().get_handle().destroy(graphics.pipeline);
		get_device().get_handle().destroy(graphics.pipeline_layout);
		get_device().get_handle().destroy(graphics.descriptor_set_layout);
		get_device().get_handle().destroy(graphics.semaphore);

		// Compute
		compute.storage_buffer.reset();
		compute.uniform_buffer.reset();
		get_device().get_handle().destroy(compute.pipeline_layout);
		get_device().get_handle().destroy(compute.descriptor_set_layout);
		get_device().get_handle().destroy(compute.pipeline_calculate);
		get_device().get_handle().destroy(compute.pipeline_integrate);
		get_device().get_handle().destroy(compute.semaphore);
		get_device().get_handle().destroy(compute.command_pool);

		get_device().get_handle().destroy(textures.particle.sampler);
		get_device().get_handle().destroy(textures.gradient.sampler);
	}
}

void ComputeNBody::get_device_features()
{
	// Enable anisotropic filtering if supported
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	}
}

void ComputeNBody::load_assets()
{
	textures.particle = load_texture("textures/particle_rgba.ktx");
	textures.gradient = load_texture("textures/particle_gradient_rgba.ktx");
}

void ComputeNBody::build_command_buffers()
{
	// Destroy command buffers if already present
	if (!check_command_buffers())
	{
		destroy_command_buffers();
		create_command_buffers();
	}

	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	vk::ClearValue clear_values[2];
	clear_values[0].color        = std::array<float, 4>{{0.0f, 0.0f, 0.0f, 1.0f}};
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
		// Set target frame buffer
		render_pass_begin_info.framebuffer = framebuffers[i];

		draw_cmd_buffers[i].begin(command_buffer_begin_info);

		// Draw the particle system using the update vertex buffer
		draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
		draw_cmd_buffers[i].setViewport(0, viewport);

		vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
		draw_cmd_buffers[i].setScissor(0, scissor);

		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, graphics.pipeline);
		draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, graphics.pipeline_layout, 0, graphics.descriptor_set, nullptr);

		vk::DeviceSize offsets[1] = {0};
		draw_cmd_buffers[i].bindVertexBuffers(0, 1, compute.storage_buffer->get(), offsets);
		draw_cmd_buffers[i].draw(num_particles, 1, 0, 0);

		draw_ui(draw_cmd_buffers[i]);

		draw_cmd_buffers[i].endRenderPass();
		draw_cmd_buffers[i].end();
	}
}

void ComputeNBody::build_compute_command_buffer()
{
	compute.command_buffer.begin({vk::CommandBufferUsageFlagBits::eSimultaneousUse});

	// First pass: Calculate particle movement
	// -------------------------------------------------------------------------------------------------------
	compute.command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipeline_calculate);
	compute.command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute.pipeline_layout, 0, compute.descriptor_set, nullptr);
	compute.command_buffer.dispatch(num_particles / 256, 1, 1);

	// Add memory barrier to ensure that the computer shader has finished writing to the buffer
	vk::BufferMemoryBarrier memory_barrier = vkb::initializers::buffer_memory_barrier();
	memory_barrier.buffer                  = compute.storage_buffer->get_handle();
	memory_barrier.size                    = compute.storage_buffer->get_size();
	memory_barrier.srcAccessMask           = vk::AccessFlagBits::eShaderWrite;
	memory_barrier.dstAccessMask           = vk::AccessFlagBits::eShaderRead;
	memory_barrier.srcQueueFamilyIndex     = VK_QUEUE_FAMILY_IGNORED;
	memory_barrier.dstQueueFamilyIndex     = VK_QUEUE_FAMILY_IGNORED;

	compute.command_buffer.pipelineBarrier(
	    vk::PipelineStageFlagBits::eComputeShader,
	    vk::PipelineStageFlagBits::eComputeShader,
	    {},
	    nullptr,
	    memory_barrier,
	    nullptr);

	// Second pass: Integrate particles
	// -------------------------------------------------------------------------------------------------------
	compute.command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, compute.pipeline_integrate);
	compute.command_buffer.dispatch(num_particles / 256, 1, 1);
	compute.command_buffer.end();
}

// Setup and fill the compute shader storage buffers containing the particles
void ComputeNBody::prepare_storage_buffers()
{
#if 0
	std::vector<glm::vec3> attractors = {
		glm::vec3(2.5f, 1.5f, 0.0f),
		glm::vec3(-2.5f, -1.5f, 0.0f),
	};
#else
	std::vector<glm::vec3> attractors = {
	    glm::vec3(5.0f, 0.0f, 0.0f),
	    glm::vec3(-5.0f, 0.0f, 0.0f),
	    glm::vec3(0.0f, 0.0f, 5.0f),
	    glm::vec3(0.0f, 0.0f, -5.0f),
	    glm::vec3(0.0f, 4.0f, 0.0f),
	    glm::vec3(0.0f, -8.0f, 0.0f),
	};
#endif

	num_particles = static_cast<uint32_t>(attractors.size()) * PARTICLES_PER_ATTRACTOR;

	// Initial particle positions
	std::vector<Particle> particle_buffer(num_particles);

	std::default_random_engine      rnd_engine(is_benchmark_mode() ? 0 : (unsigned) time(nullptr));
	std::normal_distribution<float> rnd_distribution(0.0f, 1.0f);

    for (uint32_t i = 0; i < static_cast<uint32_t>(attractors.size()); i++)
	{
        auto attractor_offset = i * PARTICLES_PER_ATTRACTOR;
        #pragma omp parallel for
		for (int32_t j = 0; j < PARTICLES_PER_ATTRACTOR; j++)
		{
			Particle &particle = particle_buffer[attractor_offset + j];

			// First particle in group as heavy center of gravity
			if (j == 0)
			{
				particle.pos = glm::vec4(attractors[i] * 1.5f, 90000.0f);
				particle.vel = glm::vec4(glm::vec4(0.0f));
			}
			else
			{
				// Position
				glm::vec3 position(attractors[i] + glm::vec3(rnd_distribution(rnd_engine), rnd_distribution(rnd_engine), rnd_distribution(rnd_engine)) * 0.75f);
				float     len = glm::length(glm::normalize(position - attractors[i]));
				position.y *= 2.0f - (len * len);

				// Velocity
				glm::vec3 angular  = glm::vec3(0.5f, 1.5f, 0.5f) * (((i % 2) == 0) ? 1.0f : -1.0f);
				glm::vec3 velocity = glm::cross((position - attractors[i]), angular) + glm::vec3(rnd_distribution(rnd_engine), rnd_distribution(rnd_engine), rnd_distribution(rnd_engine) * 0.025f);

				float mass   = (rnd_distribution(rnd_engine) * 0.5f + 0.5f) * 75.0f;
				particle.pos = glm::vec4(position, mass);
				particle.vel = glm::vec4(velocity, 0.0f);
			}

			// Color gradient offset
			particle.vel.w = (float) i * 1.0f / static_cast<uint32_t>(attractors.size());
		}
	}

	compute.ubo.particle_count = num_particles;

	compute.storage_buffer = std::make_unique<vkb::core::Buffer>(
	    device->stage_to_device_buffer(
	        particle_buffer,
	        vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer));
}

void ComputeNBody::setup_descriptor_pool()
{
	std::vector<vk::DescriptorPoolSize> pool_sizes{
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 2),
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eStorageBuffer, 1),
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 2),
	};

	descriptor_pool = get_device().get_handle().createDescriptorPool(vkb::initializers::descriptor_pool_create_info(pool_sizes, 2));
}

void ComputeNBody::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings{
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 2),
	};

	graphics.descriptor_set_layout = get_device().get_handle().createDescriptorSetLayout(vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings));

	graphics.pipeline_layout = get_device().get_handle().createPipelineLayout(vkb::initializers::pipeline_layout_create_info(graphics.descriptor_set_layout));
}

void ComputeNBody::setup_descriptor_set()
{
	vk::DescriptorSetAllocateInfo alloc_info =
	    vkb::initializers::descriptor_set_allocate_info(descriptor_pool, graphics.descriptor_set_layout);
	graphics.descriptor_set = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo buffer_descriptor         = create_descriptor(*graphics.uniform_buffer);
	vk::DescriptorImageInfo  particle_image_descriptor = create_descriptor(textures.particle);
	vk::DescriptorImageInfo  gradient_image_descriptor = create_descriptor(textures.gradient);

	std::vector<vk::WriteDescriptorSet> write_descriptor_sets{
	    vkb::initializers::write_descriptor_set(graphics.descriptor_set, vk::DescriptorType::eCombinedImageSampler, 0, &particle_image_descriptor),
	    vkb::initializers::write_descriptor_set(graphics.descriptor_set, vk::DescriptorType::eCombinedImageSampler, 1, &gradient_image_descriptor),
	    vkb::initializers::write_descriptor_set(graphics.descriptor_set, vk::DescriptorType::eUniformBuffer, 2, &buffer_descriptor),
	};
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void ComputeNBody::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info(vk::PrimitiveTopology::ePointList);

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(
	        vk::PolygonMode::eFill,
	        vk::CullModeFlagBits::eNone,
	        vk::FrontFace::eCounterClockwise);

	// Additive blending
	vk::PipelineColorBlendAttachmentState blend_attachment_state =
	    vkb::initializers::pipeline_color_blend_attachment_state();
	blend_attachment_state.blendEnable         = VK_TRUE;
	blend_attachment_state.colorBlendOp        = vk::BlendOp::eAdd;
	blend_attachment_state.srcColorBlendFactor = vk::BlendFactor::eOne;
	blend_attachment_state.dstColorBlendFactor = vk::BlendFactor::eOne;
	blend_attachment_state.alphaBlendOp        = vk::BlendOp::eAdd;
	blend_attachment_state.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
	blend_attachment_state.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(blend_attachment_state);

	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info();

	vk::PipelineViewportStateCreateInfo viewport_state =
	    vkb::initializers::pipeline_viewport_state_create_info(1, 1);

	vk::PipelineMultisampleStateCreateInfo multisample_state =
	    vkb::initializers::pipeline_multisample_state_create_info();

	std::vector<vk::DynamicState> dynamic_state_enables{
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eScissor,
	};
	vk::PipelineDynamicStateCreateInfo dynamicState =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	// Rendering pipeline
	// Load shaders
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages{
	    load_shader("compute_nbody/particle.vert", vk::ShaderStageFlagBits::eVertex),
	    load_shader("compute_nbody/particle.frag", vk::ShaderStageFlagBits::eFragment),
	};

	// Vertex bindings and attributes
	const std::vector<vk::VertexInputBindingDescription> vertex_input_bindings{
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Particle)),
	};
	const std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes{
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, pos)),
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32B32A32Sfloat, offsetof(Particle, vel)),
	};

	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(graphics.pipeline_layout, render_pass);
	pipeline_create_info.pVertexInputState   = &vertex_input_state;
	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamicState;
	pipeline_create_info.stageCount          = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages             = shader_stages.data();

	graphics.pipeline = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

void ComputeNBody::prepare_graphics()
{
	prepare_storage_buffers();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_set();

	// Semaphore for compute & graphics sync
	graphics.semaphore = get_device().get_handle().createSemaphore(vkb::initializers::semaphore_create_info());
}

void ComputeNBody::prepare_compute()
{
	auto computeQueueIndex = get_device().get_queue_family_index(vk::QueueFlagBits::eCompute);
	// Get compute queue
	compute.queue = get_device().get_handle().getQueue(computeQueueIndex, 0);

	// Create compute pipeline
	// Compute pipelines are created separate from graphics pipelines even if they use the same queue (family index)
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings = {
	    // Binding 0 : Particle position storage buffer
	    vkb::initializers::descriptor_set_layout_binding(
	        vk::DescriptorType::eStorageBuffer,
	        vk::ShaderStageFlagBits::eCompute,
	        0),
	    // Binding 1 : Uniform buffer
	    vkb::initializers::descriptor_set_layout_binding(
	        vk::DescriptorType::eUniformBuffer,
	        vk::ShaderStageFlagBits::eCompute,
	        1),
	};

	vk::DescriptorSetLayoutCreateInfo descriptor_layout =
	    vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings);

	compute.descriptor_set_layout = get_device().get_handle().createDescriptorSetLayout(descriptor_layout);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info =
	    vkb::initializers::pipeline_layout_create_info(compute.descriptor_set_layout);

	compute.pipeline_layout = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);

	vk::DescriptorSetAllocateInfo alloc_info =
	    vkb::initializers::descriptor_set_allocate_info(
	        descriptor_pool,
	        &compute.descriptor_set_layout,
	        1);

	compute.descriptor_set = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo            storage_buffer_descriptor = create_descriptor(*compute.storage_buffer);
	vk::DescriptorBufferInfo            uniform_buffer_descriptor = create_descriptor(*compute.uniform_buffer);
	std::vector<vk::WriteDescriptorSet> compute_write_descriptor_sets =
	    {
	        // Binding 0 : Particle position storage buffer
	        vkb::initializers::write_descriptor_set(
	            compute.descriptor_set,
	            vk::DescriptorType::eStorageBuffer,
	            0,
	            &storage_buffer_descriptor),
	        // Binding 1 : Uniform buffer
	        vkb::initializers::write_descriptor_set(
	            compute.descriptor_set,
	            vk::DescriptorType::eUniformBuffer,
	            1,
	            &uniform_buffer_descriptor)};

	get_device().get_handle().updateDescriptorSets(compute_write_descriptor_sets, nullptr);

	// Create pipelines
	vk::ComputePipelineCreateInfo compute_pipeline_create_info = vkb::initializers::compute_pipeline_create_info(compute.pipeline_layout);

	// 1st pass
	compute_pipeline_create_info.stage = load_shader("compute_nbody/particle_calculate.comp", vk::ShaderStageFlagBits::eCompute);

	// Set shader parameters via specialization constants
	struct SpecializationData
	{
		uint32_t shaderd_data_size;
		float    gravity;
		float    power;
		float    soften;
	} specialization_data;

	std::vector<vk::SpecializationMapEntry> specialization_map_entries;
	specialization_map_entries.push_back(vkb::initializers::specialization_map_entry(0, offsetof(SpecializationData, shaderd_data_size), sizeof(uint32_t)));
	specialization_map_entries.push_back(vkb::initializers::specialization_map_entry(1, offsetof(SpecializationData, gravity), sizeof(float)));
	specialization_map_entries.push_back(vkb::initializers::specialization_map_entry(2, offsetof(SpecializationData, power), sizeof(float)));
	specialization_map_entries.push_back(vkb::initializers::specialization_map_entry(3, offsetof(SpecializationData, soften), sizeof(float)));

	specialization_data.shaderd_data_size = std::min((uint32_t) 1024, (uint32_t)(get_device().get_properties().limits.maxComputeSharedMemorySize / sizeof(glm::vec4)));

	specialization_data.gravity = 0.002f;
	specialization_data.power   = 0.75f;
	specialization_data.soften  = 0.05f;

	vk::SpecializationInfo specialization_info =
	    vkb::initializers::specialization_info(static_cast<uint32_t>(specialization_map_entries.size()), specialization_map_entries.data(), sizeof(specialization_data), &specialization_data);
	compute_pipeline_create_info.stage.pSpecializationInfo = &specialization_info;

	compute.pipeline_calculate = get_device().get_handle().createComputePipeline(pipeline_cache, compute_pipeline_create_info);

	// 2nd pass
	compute_pipeline_create_info.stage = load_shader("compute_nbody/particle_integrate.comp", vk::ShaderStageFlagBits::eCompute);

	compute.pipeline_integrate = get_device().get_handle().createComputePipeline(pipeline_cache, compute_pipeline_create_info);

	// Separate command pool as queue family for compute may be different than graphics
	vk::CommandPoolCreateInfo command_pool_create_info = {};
	command_pool_create_info.queueFamilyIndex          = computeQueueIndex;
	command_pool_create_info.flags                     = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	compute.command_pool = get_device().get_handle().createCommandPool(command_pool_create_info);

	// Create a command buffer for compute operations
	vk::CommandBufferAllocateInfo command_buffer_allocate_info =
	    vkb::initializers::command_buffer_allocate_info(
	        compute.command_pool,
	        vk::CommandBufferLevel::ePrimary,
	        1);

	compute.command_buffer = get_device().get_handle().allocateCommandBuffers(command_buffer_allocate_info)[0];

	// Semaphore for compute & graphics sync
	compute.semaphore = get_device().get_handle().createSemaphore(vkb::initializers::semaphore_create_info());

	// Signal the semaphore
	vk::SubmitInfo submit_info;
	submit_info.signalSemaphoreCount = 1;
	submit_info.pSignalSemaphores    = &compute.semaphore;
	queue.submit(submit_info, nullptr);
	queue.waitIdle();

	// Build a single command buffer containing the compute dispatch commands
	build_compute_command_buffer();
}

// Prepare and initialize uniform buffer containing shader uniforms
void ComputeNBody::prepare_uniform_buffers()
{
	// Compute shader uniform buffer block
	compute.uniform_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                             sizeof(compute.ubo),
	                                                             vk::BufferUsageFlagBits::eUniformBuffer,
	                                                             vma::MemoryUsage::eCpuToGpu);

	// Vertex shader uniform buffer block
	graphics.uniform_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                              sizeof(graphics.ubo),
	                                                              vk::BufferUsageFlagBits::eUniformBuffer,
	                                                              vma::MemoryUsage::eCpuToGpu);

	update_compute_uniform_buffers(1.0f);
	update_graphics_uniform_buffers();
}

void ComputeNBody::update_compute_uniform_buffers(float delta_time)
{
	compute.ubo.delta_time = paused ? 0.0f : delta_time;
	compute.uniform_buffer->convert_and_update(compute.ubo);
}

void ComputeNBody::update_graphics_uniform_buffers()
{
	graphics.ubo.projection = camera.matrices.perspective;
	graphics.ubo.view       = camera.matrices.view;
	graphics.ubo.screenDim  = glm::vec2((float) width, (float) height);
	graphics.uniform_buffer->convert_and_update(graphics.ubo);
}

void ComputeNBody::draw()
{
	ApiVulkanSample::prepare_frame();

	vk::PipelineStageFlags graphics_wait_stage_masks[]  = {vk::PipelineStageFlagBits::eVertexInput, vk::PipelineStageFlagBits::eColorAttachmentOutput};
	vk::Semaphore          graphics_wait_semaphores[]   = {compute.semaphore, semaphores.acquired_image_ready};
	vk::Semaphore          graphics_signal_semaphores[] = {graphics.semaphore, semaphores.render_complete};

	// Submit graphics commands
	submit_info.commandBufferCount   = 1;
	submit_info.pCommandBuffers      = &draw_cmd_buffers[current_buffer];
	submit_info.waitSemaphoreCount   = 2;
	submit_info.pWaitSemaphores      = graphics_wait_semaphores;
	submit_info.pWaitDstStageMask    = graphics_wait_stage_masks;
	submit_info.signalSemaphoreCount = 2;
	submit_info.pSignalSemaphores    = graphics_signal_semaphores;
	queue.submit(submit_info, nullptr);

	ApiVulkanSample::submit_frame();

	// Wait for rendering finished
	vk::PipelineStageFlags wait_stage_mask = vk::PipelineStageFlagBits::eComputeShader;

	// Submit compute commands
	vk::SubmitInfo compute_submit_info       = vkb::initializers::submit_info();
	compute_submit_info.commandBufferCount   = 1;
	compute_submit_info.pCommandBuffers      = &compute.command_buffer;
	compute_submit_info.waitSemaphoreCount   = 1;
	compute_submit_info.pWaitSemaphores      = &graphics.semaphore;
	compute_submit_info.pWaitDstStageMask    = &wait_stage_mask;
	compute_submit_info.signalSemaphoreCount = 1;
	compute_submit_info.pSignalSemaphores    = &compute.semaphore;
	compute.queue.submit(compute_submit_info, nullptr);
}

bool ComputeNBody::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}
	load_assets();
	setup_descriptor_pool();
	prepare_graphics();
	prepare_compute();
	build_command_buffers();
	prepared = true;
	return true;
}

void ComputeNBody::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
	update_compute_uniform_buffers(delta_time);
	if (camera.updated)
	{
		update_graphics_uniform_buffers();
	}
}

void ComputeNBody::resize(const uint32_t width, const uint32_t height)
{
	ApiVulkanSample::resize(width, height);
	update_graphics_uniform_buffers();
}

std::unique_ptr<vkb::Application> create_compute_nbody()
{
	return std::make_unique<ComputeNBody>();
}
