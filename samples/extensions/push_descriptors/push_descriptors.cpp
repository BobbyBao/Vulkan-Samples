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
 * Push descriptors
 *
 * Note: Requires a device that supports the VK_KHR_push_descriptor extension
 *
 * Push descriptors apply the push constants concept to descriptor sets. So instead of creating 
 * per-model descriptor sets (along with a pool for each descriptor type) for rendering multiple objects, 
 * this example uses push descriptors to pass descriptor sets for per-model textures and matrices 
 * at command buffer creation time.
 */

#include "push_descriptors.h"

#include "core/buffer.h"
#include "scene_graph/components/sub_mesh.h"

PushDescriptors::PushDescriptors()
{
	title = "Push descriptors";

	// Enable extension required for push descriptors
	instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
}

PushDescriptors::~PushDescriptors()
{
	if (device)
	{
		get_device().get_handle().destroy(pipeline);
		get_device().get_handle().destroy(pipeline_layout);
		get_device().get_handle().destroy(descriptor_set_layout);
		for (auto &cube : cubes)
		{
			cube.uniform_buffer.reset();
			cube.texture.image.reset();
			get_device().get_handle().destroy(cube.texture.sampler);
		}
		uniform_buffers.scene.reset();
	}
}

void PushDescriptors::get_device_features()
{
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	}
}

void PushDescriptors::build_command_buffers()
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

		draw_cmd_buffers[i].begin(&command_buffer_begin_info);

		draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

		vk::Viewport viewport = vkb::initializers::viewport(static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f);
		draw_cmd_buffers[i].setViewport(0, viewport);

		vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
		draw_cmd_buffers[i].setScissor(0, scissor);

		const auto &vertex_buffer = models.cube->vertex_buffers.at("vertex_buffer");
		auto &      index_buffer  = models.cube->index_buffer;

		draw_cmd_buffers[i].bindVertexBuffers(0, vertex_buffer.get_handle(), {0});
		draw_cmd_buffers[i].bindIndexBuffer(index_buffer->get_handle(), 0, models.cube->index_type);

		// Render two cubes using different descriptor sets using push descriptors
		for (auto &cube : cubes)
		{
			// Instead of preparing the descriptor sets up-front, using push descriptors we can set (push) them inside of a command buffer
			// This allows a more dynamic approach without the need to create descriptor sets for each model
			// Note: dstSet for each descriptor set write is left at zero as this is ignored when ushing push descriptors

			std::array<vk::WriteDescriptorSet, 3> write_descriptor_sets{};

			// Scene matrices
			vk::DescriptorBufferInfo scene_buffer_descriptor = create_descriptor(*uniform_buffers.scene);
			//write_descriptor_sets[0].dstSet                = 0;
			write_descriptor_sets[0].dstBinding      = 0;
			write_descriptor_sets[0].descriptorCount = 1;
			write_descriptor_sets[0].descriptorType  = vk::DescriptorType::eUniformBuffer;
			write_descriptor_sets[0].pBufferInfo     = &scene_buffer_descriptor;

			// Model matrices
			vk::DescriptorBufferInfo cube_buffer_descriptor = create_descriptor(*cube.uniform_buffer);
			//write_descriptor_sets[1].dstSet               = 0;
			write_descriptor_sets[1].dstBinding      = 1;
			write_descriptor_sets[1].descriptorCount = 1;
			write_descriptor_sets[1].descriptorType  = vk::DescriptorType::eUniformBuffer;
			write_descriptor_sets[1].pBufferInfo     = &cube_buffer_descriptor;

			// Texture
			vk::DescriptorImageInfo image_descriptor = create_descriptor(cube.texture);
			//write_descriptor_sets[2].dstSet          = 0;
			write_descriptor_sets[2].dstBinding      = 2;
			write_descriptor_sets[2].descriptorCount = 1;
			write_descriptor_sets[2].descriptorType  = vk::DescriptorType::eCombinedImageSampler;
			write_descriptor_sets[2].pImageInfo      = &image_descriptor;

			draw_cmd_buffers[i].pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, write_descriptor_sets);

			draw_model(models.cube, draw_cmd_buffers[i]);
		}

		draw_ui(draw_cmd_buffers[i]);

		draw_cmd_buffers[i].endRenderPass();

		draw_cmd_buffers[i].end();
	}
}

void PushDescriptors::load_assets()
{
	models.cube      = load_model("scenes/textured_unit_cube.gltf");
	cubes[0].texture = load_texture("textures/crate01_color_height_rgba.ktx");
	cubes[1].texture = load_texture("textures/crate02_color_height_rgba.ktx");
}

void PushDescriptors::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2),
	};

	vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info;
	// Setting this flag tells the descriptor set layouts that no actual descriptor sets are allocated but instead pushed at command buffer creation time
	descriptor_layout_create_info.flags        = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR;
	descriptor_layout_create_info.bindingCount = static_cast<uint32_t>(set_layout_bindings.size());
	descriptor_layout_create_info.pBindings    = set_layout_bindings.data();
	descriptor_set_layout                      = get_device().get_handle().createDescriptorSetLayout(descriptor_layout_create_info);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layout, 1);

	pipeline_layout = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);
}

void PushDescriptors::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info(vk::PrimitiveTopology::eTriangleList);

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise);

	vk::PipelineColorBlendAttachmentState blend_attachment_state =
	    vkb::initializers::pipeline_color_blend_attachment_state();

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(1, &blend_attachment_state);

	// Note: Using Reversed depth-buffer for increased precision, so Greater depth values are kept
	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info(VK_TRUE, VK_TRUE, vk::CompareOp::eGreater);

	vk::PipelineViewportStateCreateInfo viewport_state =
	    vkb::initializers::pipeline_viewport_state_create_info();

	vk::PipelineMultisampleStateCreateInfo multisample_state =
	    vkb::initializers::pipeline_multisample_state_create_info();

	std::vector<vk::DynamicState> dynamic_state_enables{
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eScissor,
	};

	vk::PipelineDynamicStateCreateInfo dynamic_state =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	// Vertex bindings and attributes
	const std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex)),
	};
	const std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, 0),                        // Location 0: Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3),        // Location 1: Normal
	    vkb::initializers::vertex_input_attribute_description(0, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6),           // Location 2: UV
	    vkb::initializers::vertex_input_attribute_description(0, 3, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8),        // Location 3: Color
	};
	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();

	vertex_input_state.vertexBindingDescriptionCount   = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions      = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions    = vertex_input_attributes.data();

	vk::GraphicsPipelineCreateInfo pipeline_create_info = vkb::initializers::pipeline_create_info(pipeline_layout, render_pass);

	pipeline_create_info.pVertexInputState   = &vertex_input_state;
	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;

	const std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages = {
	    load_shader("push_descriptors/cube.vert", vk::ShaderStageFlagBits::eVertex),
	    load_shader("push_descriptors/cube.frag", vk::ShaderStageFlagBits::eFragment)};

	pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages    = shader_stages.data();

	pipeline = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

void PushDescriptors::prepare_uniform_buffers()
{
	// Vertex shader scene uniform buffer block
	uniform_buffers.scene = std::make_unique<vkb::core::Buffer>(
	    get_device(),
	    sizeof(UboScene),
	    vk::BufferUsageFlagBits::eUniformBuffer,
	    vma::MemoryUsage::eCpuToGpu);

	// Vertex shader cube model uniform buffer blocks
	for (auto &cube : cubes)
	{
		cube.uniform_buffer = std::make_unique<vkb::core::Buffer>(
		    get_device(),
		    sizeof(glm::mat4),
		    vk::BufferUsageFlagBits::eUniformBuffer,
		    vma::MemoryUsage::eCpuToGpu);
	}

	update_uniform_buffers();
	update_cube_uniform_buffers(0.0f);
}

void PushDescriptors::update_uniform_buffers()
{
	ubo_scene.projection = camera.matrices.perspective;
	ubo_scene.view       = camera.matrices.view;
	uniform_buffers.scene->convert_and_update(ubo_scene);
}

void PushDescriptors::update_cube_uniform_buffers(float delta_time)
{
	cubes[0].model_mat = glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f));
	cubes[1].model_mat = glm::translate(glm::mat4(1.0f), glm::vec3(1.5f, 0.5f, 0.0f));

	for (auto &cube : cubes)
	{
		cube.model_mat = glm::rotate(cube.model_mat, glm::radians(cube.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		cube.model_mat = glm::rotate(cube.model_mat, glm::radians(cube.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		cube.model_mat = glm::rotate(cube.model_mat, glm::radians(cube.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
		cube.uniform_buffer->convert_and_update(cube.model_mat);
	}

	if (animate)
	{
		cubes[0].rotation.x += 2.5f * delta_time;
		if (cubes[0].rotation.x > 360.0f)
		{
			cubes[0].rotation.x -= 360.0f;
		}
		cubes[1].rotation.y += 2.0f * delta_time;
		if (cubes[1].rotation.x > 360.0f)
		{
			cubes[1].rotation.x -= 360.0f;
		}
	}
}

void PushDescriptors::draw()
{
	ApiVulkanSample::prepare_frame();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];
	queue.submit(submit_info, nullptr);
	ApiVulkanSample::submit_frame();
}

bool PushDescriptors::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	/*
		Extension specific functions
	*/

	// The push descriptor update function is part of an extension so it has to be manually loaded
	if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPushDescriptorSetKHR)
	{
		throw std::runtime_error("Could not get a valid function pointer for vkCmdPushDescriptorSetKHR");
	}

	// Get device push descriptor properties (to display them)
	auto structure_chain       = get_device().get_physical_device().getProperties2KHR<vk::PhysicalDeviceProperties2KHR, vk::PhysicalDevicePushDescriptorPropertiesKHR>();
	push_descriptor_properties = structure_chain.get<vk::PhysicalDevicePushDescriptorPropertiesKHR>();

	/*
		End of extension specific functions
	*/

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.type = vkb::CameraType::LookAt;
	camera.set_perspective(60.0f, static_cast<float>(width) / height, 512.0f, 0.1f);
	camera.set_rotation(glm::vec3(0.0f, 0.0f, 0.0f));
	camera.set_translation(glm::vec3(0.0f, 0.0f, -5.0f));

	load_assets();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	build_command_buffers();
	prepared = true;
	return true;
}

void PushDescriptors::render(float delta_time)
{
	if (!prepared)
	{
		return;
	}

	draw();
	if (animate)
	{
		update_cube_uniform_buffers(delta_time);
	}
	if (camera.updated)
	{
		update_uniform_buffers();
	}
}

void PushDescriptors::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Settings"))
	{
		drawer.checkbox("Animate", &animate);
	}
	if (drawer.header("Device properties"))
	{
		drawer.text("maxPushDescriptors: %d", push_descriptor_properties.maxPushDescriptors);
	}
}

std::unique_ptr<vkb::VulkanSample> create_push_descriptors()
{
	return std::make_unique<PushDescriptors>();
}
