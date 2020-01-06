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
 * Instanced mesh rendering, uses a separate vertex buffer for instanced data
 */

#include "instancing.h"

Instancing::Instancing()
{
	title = "Instanced mesh rendering";
}

Instancing::~Instancing()
{
	if (device)
	{
		get_device().get_handle().destroy(pipelines.instanced_rocks);
		get_device().get_handle().destroy(pipelines.planet);
		get_device().get_handle().destroy(pipelines.starfield);
		get_device().get_handle().destroy(pipeline_layout);
		get_device().get_handle().destroy(descriptor_set_layout);
		get_device().get_handle().destroy(instance_buffer.buffer);
		get_device().get_handle().freeMemory(instance_buffer.memory);
		get_device().get_handle().destroy(textures.rocks.sampler);
		get_device().get_handle().destroy(textures.planet.sampler);
	}
}

void Instancing::get_device_features()
{
	// Enable anisotropic filtering if supported
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	}
	// Enable texture compression
	if (supported_device_features.textureCompressionBC)
	{
		requested_device_features.textureCompressionBC = VK_TRUE;
	}
	else if (supported_device_features.textureCompressionASTC_LDR)
	{
		requested_device_features.textureCompressionASTC_LDR = VK_TRUE;
	}
	else if (supported_device_features.textureCompressionETC2)
	{
		requested_device_features.textureCompressionETC2 = VK_TRUE;
	}
};

void Instancing::update_draw_command_buffer(const vk::CommandBuffer &draw_cmd_buffer)
{
	ApiVulkanSample::update_draw_command_buffer(draw_cmd_buffer);

	vk::DeviceSize offsets[1] = {0};

	// Star field
	draw_cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &descriptor_sets.planet, 0, NULL);
	draw_cmd_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.starfield);
	draw_cmd_buffer.draw(4, 1, 0, 0);

	// Planet
	auto &planet_vertex_buffer = models.planet->vertex_buffers.at("vertex_buffer");
	auto &planet_index_buffer  = models.planet->index_buffer;
	draw_cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &descriptor_sets.planet, 0, NULL);
	draw_cmd_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.planet);
	draw_cmd_buffer.bindVertexBuffers(0, 1, planet_vertex_buffer.get(), offsets);
	draw_cmd_buffer.bindIndexBuffer(planet_index_buffer->get_handle(), 0, vk::IndexType::eUint32);
	draw_cmd_buffer.drawIndexed(models.planet->vertex_indices, 1, 0, 0, 0);

	// Instanced rocks
	auto &rock_vertex_buffer = models.rock->vertex_buffers.at("vertex_buffer");
	auto &rock_index_buffer  = models.rock->index_buffer;
	draw_cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &descriptor_sets.instanced_rocks, 0, NULL);
	draw_cmd_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.instanced_rocks);
	// Binding point 0 : Mesh vertex buffer
	draw_cmd_buffer.bindVertexBuffers(0, 1, rock_vertex_buffer.get(), offsets);
	// Binding point 1 : Instance data buffer
	draw_cmd_buffer.bindVertexBuffers(1, 1, &instance_buffer.buffer, offsets);
	draw_cmd_buffer.bindIndexBuffer(rock_index_buffer->get_handle(), 0, vk::IndexType::eUint32);
	// Render instances
	draw_cmd_buffer.drawIndexed(models.rock->vertex_indices, INSTANCE_COUNT, 0, 0, 0);
}

void Instancing::load_assets()
{
	models.rock   = load_model("scenes/rock.gltf");
	models.planet = load_model("scenes/planet.gltf");

	//models.rock.loadFromFile(getAssetPath() + "scenes/rock.gltf", device.get(), queue);
	//models.planet.loadFromFile(getAssetPath() + "scenes/planet.gltf", device.get(), queue);

	textures.rocks  = load_texture_array("textures/texturearray_rocks_color_rgba.ktx");
	textures.planet = load_texture("textures/lavaplanet_color_rgba.ktx");

	//textures.rocks.loadFromFile(getAssetPath() + "textures/texturearray_rocks_color_rgba.ktx", device.get(), queue);
	//textures.planet.loadFromFile(getAssetPath() + "textures/lavaplanet_color_rgba.ktx", device.get(), queue);
}

void Instancing::setup_descriptor_pool()
{
	// Example uses one ubo
	std::vector<vk::DescriptorPoolSize> pool_sizes =
	    {
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 2),
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 2),
	    };

	vk::DescriptorPoolCreateInfo descriptor_pool_create_info =
	    vkb::initializers::descriptor_pool_create_info(pool_sizes, 2);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);
}

void Instancing::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings =
	    {
	        // Binding 0 : Vertex shader uniform buffer
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eUniformBuffer,
	            vk::ShaderStageFlagBits::eVertex,
	            0),
	        // Binding 1 : Fragment shader combined sampler
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eCombinedImageSampler,
	            vk::ShaderStageFlagBits::eFragment,
	            1),
	    };

	vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info =
	    vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings);

	descriptor_set_layout = get_device().get_handle().createDescriptorSetLayout(descriptor_layout_create_info);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info =
	    vkb::initializers::pipeline_layout_create_info(
	        &descriptor_set_layout,
	        1);

	pipeline_layout = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);
}

void Instancing::setup_descriptor_set()
{
	vk::DescriptorSetAllocateInfo descriptor_set_alloc_info;

	descriptor_set_alloc_info = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layout, 1);

	// Instanced rocks
	vk::DescriptorBufferInfo buffer_descriptor       = create_descriptor(*uniform_buffers.scene);
	vk::DescriptorImageInfo  rocks_image_descriptor  = create_descriptor(textures.rocks);
	vk::DescriptorImageInfo  planet_image_descriptor = create_descriptor(textures.planet);

	descriptor_sets.instanced_rocks = get_device().get_handle().allocateDescriptorSets(descriptor_set_alloc_info)[0];
	// Planet
	descriptor_sets.planet = get_device().get_handle().allocateDescriptorSets(descriptor_set_alloc_info)[0];

	std::vector<vk::WriteDescriptorSet> write_descriptor_sets{
	    vkb::initializers::write_descriptor_set(descriptor_sets.instanced_rocks, vk::DescriptorType::eUniformBuffer, 0, &buffer_descriptor),              // Binding 0 : Vertex shader uniform buffer
	    vkb::initializers::write_descriptor_set(descriptor_sets.instanced_rocks, vk::DescriptorType::eCombinedImageSampler, 1, &rocks_image_descriptor),        // Binding 1 : Color map
	    vkb::initializers::write_descriptor_set(descriptor_sets.planet, vk::DescriptorType::eUniformBuffer, 0, &buffer_descriptor),                       // Binding 0 : Vertex shader uniform buffer
	    vkb::initializers::write_descriptor_set(descriptor_sets.planet, vk::DescriptorType::eCombinedImageSampler, 1, &planet_image_descriptor),                 // Binding 1 : Color map
	};
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void Instancing::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info(
	        vk::PrimitiveTopology::eTriangleList);

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(
	        vk::CullModeFlagBits::eBack,
	        vk::FrontFace::eClockwise);

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
	    vkb::initializers::pipeline_viewport_state_create_info(1, 1);

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

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(pipeline_layout, render_pass);

	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;
	pipeline_create_info.stageCount          = vkb::to_u32(shader_stages.size());
	pipeline_create_info.pStages             = shader_stages.data();

	// This example uses two different input states, one for the instanced part and one for non-instanced rendering
	vk::PipelineVertexInputStateCreateInfo           input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	std::vector<vk::VertexInputBindingDescription>   binding_descriptions;
	std::vector<vk::VertexInputAttributeDescription> attribute_descriptions;

	// Vertex input bindings
	// The instancing pipeline uses a vertex input state with two bindings
	binding_descriptions = {
	    // Binding point 0: Mesh vertex layout description at per-vertex rate
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex)),
	    // Binding point 1: Instanced data at per-instance rate
	    vkb::initializers::vertex_input_binding_description(1, sizeof(InstanceData), vk::VertexInputRate::eInstance)};

	// Vertex attribute bindings
	// Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
	// instanced.vert:
	//	layout (location = 0) in vec3 inPos;		Per-Vertex
	//	...
	//	layout (location = 4) in vec3 instancePos;	Per-Instance
	attribute_descriptions = {
	    // Per-vertex attributees
	    // These are advanced for each vertex fetched by the vertex shader
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, 0),                        // Location 0: Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3),        // Location 1: Normal
	    vkb::initializers::vertex_input_attribute_description(0, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6),           // Location 2: Texture coordinates
	    vkb::initializers::vertex_input_attribute_description(0, 3, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8),        // Location 3: Color
	    // Per-Instance attributes
	    // These are fetched for each instance rendered
	    vkb::initializers::vertex_input_attribute_description(1, 4, vk::Format::eR32G32B32Sfloat, 0),                        // Location 4: Position
	    vkb::initializers::vertex_input_attribute_description(1, 5, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3),        // Location 5: Rotation
	    vkb::initializers::vertex_input_attribute_description(1, 6, vk::Format::eR32Sfloat, sizeof(float) * 6),              // Location 6: Scale
	    vkb::initializers::vertex_input_attribute_description(1, 7, vk::Format::eR32Sint, sizeof(float) * 7),                // Location 7: Texture array layer index
	};
	input_state.pVertexBindingDescriptions   = binding_descriptions.data();
	input_state.pVertexAttributeDescriptions = attribute_descriptions.data();

	pipeline_create_info.pVertexInputState = &input_state;

	// Instancing pipeline
	shader_stages[0] = load_shader("instancing/instancing.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("instancing/instancing.frag", vk::ShaderStageFlagBits::eFragment);
	// Use all input bindings and attribute descriptions
	input_state.vertexBindingDescriptionCount   = static_cast<uint32_t>(binding_descriptions.size());
	input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribute_descriptions.size());
	pipelines.instanced_rocks                   = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Planet rendering pipeline
	shader_stages[0] = load_shader("instancing/planet.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("instancing/planet.frag", vk::ShaderStageFlagBits::eFragment);
	// Only use the non-instanced input bindings and attribute descriptions
	input_state.vertexBindingDescriptionCount   = 1;
	input_state.vertexAttributeDescriptionCount = 4;
	pipelines.planet                            = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Star field pipeline
	rasterization_state.cullMode         = vk::CullModeFlagBits::eNone;
	depth_stencil_state.depthWriteEnable = VK_FALSE;
	shader_stages[0]                     = load_shader("instancing/starfield.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1]                     = load_shader("instancing/starfield.frag", vk::ShaderStageFlagBits::eFragment);
	// Vertices are generated in the vertex shader
	input_state.vertexBindingDescriptionCount   = 0;
	input_state.vertexAttributeDescriptionCount = 0;
	pipelines.starfield                         = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

void Instancing::prepare_instance_data()
{
	std::vector<InstanceData> instance_data;
	instance_data.resize(INSTANCE_COUNT);

	std::default_random_engine              rnd_generator(is_benchmark_mode() ? 0 : (unsigned) time(nullptr));
	std::uniform_real_distribution<float>   uniform_dist(0.0, 1.0);
	std::uniform_int_distribution<uint32_t> rnd_texture_index(0, textures.rocks.image->get_vk_image().get_array_layer_count());

	// Distribute rocks randomly on two different rings
	for (auto i = 0; i < INSTANCE_COUNT / 2; i++)
	{
		glm::vec2 ring0{7.0f, 11.0f};
		glm::vec2 ring1{14.0f, 18.0f};

		float rho, theta;

		// Inner ring
		rho                       = sqrt((pow(ring0[1], 2.0f) - pow(ring0[0], 2.0f)) * uniform_dist(rnd_generator) + pow(ring0[0], 2.0f));
		theta                     = 2.0f * glm::pi<float>() * uniform_dist(rnd_generator);
		instance_data[i].pos      = glm::vec3(rho * cos(theta), uniform_dist(rnd_generator) * 0.5f - 0.25f, rho * sin(theta));
		instance_data[i].rot      = glm::vec3(glm::pi<float>() * uniform_dist(rnd_generator), glm::pi<float>() * uniform_dist(rnd_generator), glm::pi<float>() * uniform_dist(rnd_generator));
		instance_data[i].scale    = 1.5f + uniform_dist(rnd_generator) - uniform_dist(rnd_generator);
		instance_data[i].texIndex = rnd_texture_index(rnd_generator);
		instance_data[i].scale *= 0.75f;

		// Outer ring
		rho                                                                 = sqrt((pow(ring1[1], 2.0f) - pow(ring1[0], 2.0f)) * uniform_dist(rnd_generator) + pow(ring1[0], 2.0f));
		theta                                                               = 2.0f * glm::pi<float>() * uniform_dist(rnd_generator);
		instance_data[static_cast<size_t>(i + INSTANCE_COUNT / 2)].pos      = glm::vec3(rho * cos(theta), uniform_dist(rnd_generator) * 0.5f - 0.25f, rho * sin(theta));
		instance_data[static_cast<size_t>(i + INSTANCE_COUNT / 2)].rot      = glm::vec3(glm::pi<float>() * uniform_dist(rnd_generator), glm::pi<float>() * uniform_dist(rnd_generator), glm::pi<float>() * uniform_dist(rnd_generator));
		instance_data[static_cast<size_t>(i + INSTANCE_COUNT / 2)].scale    = 1.5f + uniform_dist(rnd_generator) - uniform_dist(rnd_generator);
		instance_data[static_cast<size_t>(i + INSTANCE_COUNT / 2)].texIndex = rnd_texture_index(rnd_generator);
		instance_data[static_cast<size_t>(i + INSTANCE_COUNT / 2)].scale *= 0.75f;
	}

	instance_buffer.size = instance_data.size() * sizeof(InstanceData);

	// Staging
	// Instanced data is static, copy to device local memory
	// On devices with separate memory types for host visible and device local memory this will result in better performance
	// On devices with unified memory types (DEVICE_LOCAL_BIT and HOST_VISIBLE_BIT supported at once) this isn't necessary and you could skip the staging

	struct
	{
		vk::DeviceMemory memory;
		vk::Buffer       buffer;
	} staging_buffer;

	staging_buffer.buffer = get_device().create_buffer(
	    vk::BufferUsageFlagBits::eTransferSrc,
	    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
	    instance_buffer.size,
	    &staging_buffer.memory,
	    instance_data.data());

	instance_buffer.buffer = get_device().create_buffer(
	    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
	    vk::MemoryPropertyFlagBits::eDeviceLocal,
	    instance_buffer.size,
	    &instance_buffer.memory);

	// Copy to staging buffer
	vk::CommandBuffer copy_command = device->create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

	copy_command.copyBuffer(
	    staging_buffer.buffer,
	    instance_buffer.buffer,
	    vk::BufferCopy{0, 0, instance_buffer.size});

	device->flush_command_buffer(copy_command, queue, true);

	instance_buffer.descriptor.range  = instance_buffer.size;
	instance_buffer.descriptor.buffer = instance_buffer.buffer;
	instance_buffer.descriptor.offset = 0;

	// Destroy staging resources
	get_device().get_handle().destroy(staging_buffer.buffer, nullptr);
	get_device().get_handle().freeMemory(staging_buffer.memory);
}

void Instancing::prepare_uniform_buffers()
{
	uniform_buffers.scene = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                            sizeof(ubo_vs),
	                                                            vk::BufferUsageFlagBits::eUniformBuffer,
	                                                            vma::MemoryUsage::eCpuToGpu);

	update_uniform_buffer(0.0f);
}

void Instancing::update_uniform_buffer(float delta_time)
{
	ubo_vs.projection = camera.matrices.perspective;
	ubo_vs.view       = camera.matrices.view;

	if (!paused)
	{
		ubo_vs.loc_speed += delta_time * 0.35f;
		ubo_vs.glob_speed += delta_time * 0.01f;
	}

	uniform_buffers.scene->convert_and_update(ubo_vs);
}

void Instancing::draw()
{
	ApiVulkanSample::prepare_frame();

	// Command buffer to be sumitted to the queue
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];

	// Submit to queue
	queue.submit(submit_info, nullptr);

	ApiVulkanSample::submit_frame();
}

bool Instancing::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.type = vkb::CameraType::LookAt;
	camera.set_perspective(60.0f, (float) width / (float) height, 256.0f, 0.1f);
	camera.set_rotation(glm::vec3(-17.2f, -4.7f, 0.0f));
	camera.set_translation(glm::vec3(5.5f, -1.85f, -18.5f));

	load_assets();
	prepare_instance_data();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_set();
	build_command_buffers();
	prepared = true;
	return true;
}

void Instancing::render(float delta_time)
{
	if (!prepared)
	{
		return;
	}
	draw();
	if (!paused || camera.updated)
	{
		update_uniform_buffer(delta_time);
	}
}

void Instancing::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Statistics"))
	{
		drawer.text("Instances: %d", INSTANCE_COUNT);
	}
}

void Instancing::resize(const uint32_t width, const uint32_t height)
{
	ApiVulkanSample::resize(width, height);
	build_command_buffers();
}

std::unique_ptr<vkb::Application> create_instancing()
{
	return std::make_unique<Instancing>();
}
