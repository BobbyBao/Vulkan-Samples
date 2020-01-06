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
 * Conservative rasterization
 *
 * Note: Requires a device that supports the VK_EXT_conservative_rasterization extension
 *
 * Uses an offscreen buffer with lower resolution to demonstrate the effect of conservative rasterization
 */

#include "conservative_rasterization.h"

#define FB_COLOR_FORMAT vk::Format::eR8G8B8A8Unorm
#define ZOOM_FACTOR 16

ConservativeRasterization::ConservativeRasterization()
{
	title = "Conservative rasterization";

	// Reading device properties of conservative rasterization requires VK_KHR_get_physical_device_properties2 to be enabled
	instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

	// Enable extension required for conservative rasterization
	device_extensions.push_back(VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME);
}

ConservativeRasterization::~ConservativeRasterization()
{
	if (device)
	{
		get_device().get_handle().destroy(offscreen_pass.color.view);
		get_device().get_handle().destroy(offscreen_pass.color.image);
		get_device().get_handle().freeMemory(offscreen_pass.color.mem, nullptr);
		get_device().get_handle().destroy(offscreen_pass.depth.view);
		get_device().get_handle().destroy(offscreen_pass.depth.image);
		get_device().get_handle().freeMemory(offscreen_pass.depth.mem, nullptr);

		get_device().get_handle().destroy(offscreen_pass.render_pass);
		get_device().get_handle().destroy(offscreen_pass.sampler);
		get_device().get_handle().destroy(offscreen_pass.framebuffer);

		get_device().get_handle().destroy(pipelines.triangle);
		get_device().get_handle().destroy(pipelines.triangle_overlay);
		get_device().get_handle().destroy(pipelines.triangle_conservative_raster);
		get_device().get_handle().destroy(pipelines.fullscreen);

		get_device().get_handle().destroy(pipeline_layouts.fullscreen);
		get_device().get_handle().destroy(pipeline_layouts.scene);

		get_device().get_handle().destroy(descriptor_set_layouts.scene);
		get_device().get_handle().destroy(descriptor_set_layouts.fullscreen);
	}

	uniform_buffers.scene.reset();
	triangle.vertices.reset();
	triangle.indices.reset();
}

void ConservativeRasterization::get_device_features()
{
	requested_device_features.fillModeNonSolid = supported_device_features.fillModeNonSolid;
	requested_device_features.wideLines        = supported_device_features.wideLines;
}

// Setup offscreen framebuffer, attachments and render passes for lower resolution rendering of the scene
void ConservativeRasterization::prepare_offscreen()
{
	offscreen_pass.width  = width / ZOOM_FACTOR;
	offscreen_pass.height = height / ZOOM_FACTOR;

	// Find a suitable depth format
	vk::Format framebuffer_depth_format = vkb::get_supported_depth_format(get_device().get_physical_device());
	assert(framebuffer_depth_format != vk::Format::eUndefined);

	// Color attachment
	vk::ImageCreateInfo image = vkb::initializers::image_create_info();
	image.imageType           = vk::ImageType::e2D;
	image.format              = FB_COLOR_FORMAT;
	image.extent.width        = offscreen_pass.width;
	image.extent.height       = offscreen_pass.height;
	image.extent.depth        = 1;
	image.mipLevels           = 1;
	image.arrayLayers         = 1;
	image.samples             = vk::SampleCountFlagBits::e1;
	image.tiling              = vk::ImageTiling::eOptimal;
	// We will sample directly from the color attachment
	image.usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;

	vk::MemoryAllocateInfo memory_allocation_info = vkb::initializers::memory_allocate_info();
	vk::MemoryRequirements memory_requirements;

	offscreen_pass.color.image             = get_device().get_handle().createImage(image);
	memory_requirements                    = get_device().get_handle().getImageMemoryRequirements(offscreen_pass.color.image);
	memory_allocation_info.allocationSize  = memory_requirements.size;
	memory_allocation_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	offscreen_pass.color.mem               = get_device().get_handle().allocateMemory(memory_allocation_info);
	get_device().get_handle().bindImageMemory(offscreen_pass.color.image, offscreen_pass.color.mem, 0);

	vk::ImageViewCreateInfo color_image_view         = vkb::initializers::image_view_create_info();
	color_image_view.viewType                        = vk::ImageViewType::e2D;
	color_image_view.format                          = FB_COLOR_FORMAT;
	color_image_view.subresourceRange                = {};
	color_image_view.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
	color_image_view.subresourceRange.baseMipLevel   = 0;
	color_image_view.subresourceRange.levelCount     = 1;
	color_image_view.subresourceRange.baseArrayLayer = 0;
	color_image_view.subresourceRange.layerCount     = 1;
	color_image_view.image                           = offscreen_pass.color.image;
	offscreen_pass.color.view                        = get_device().get_handle().createImageView(color_image_view);

	// Create sampler to sample from the attachment in the fragment shader
	vk::SamplerCreateInfo sampler_info = vkb::initializers::sampler_create_info();
	sampler_info.magFilter             = vk::Filter::eNearest;
	sampler_info.minFilter             = vk::Filter::eNearest;
	sampler_info.mipmapMode            = vk::SamplerMipmapMode::eLinear;
	sampler_info.addressModeU          = vk::SamplerAddressMode::eClampToEdge;
	sampler_info.addressModeV          = sampler_info.addressModeU;
	sampler_info.addressModeW          = sampler_info.addressModeU;
	sampler_info.mipLodBias            = 0.0f;
	sampler_info.maxAnisotropy         = 1.0f;
	sampler_info.minLod                = 0.0f;
	sampler_info.maxLod                = 1.0f;
	sampler_info.borderColor           = vk::BorderColor::eFloatOpaqueWhite;
	offscreen_pass.sampler             = get_device().get_handle().createSampler(sampler_info);

	// Depth stencil attachment
	image.format = framebuffer_depth_format;
	image.usage  = vk::ImageUsageFlagBits::eDepthStencilAttachment;

	offscreen_pass.depth.image             = get_device().get_handle().createImage(image);
	memory_requirements                    = get_device().get_handle().getImageMemoryRequirements(offscreen_pass.depth.image);
	memory_allocation_info.allocationSize  = memory_requirements.size;
	memory_allocation_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	offscreen_pass.depth.mem               = get_device().get_handle().allocateMemory(memory_allocation_info);
	get_device().get_handle().bindImageMemory(offscreen_pass.depth.image, offscreen_pass.depth.mem, 0);

	vk::ImageViewCreateInfo depth_stencil_view     = vkb::initializers::image_view_create_info();
	depth_stencil_view.viewType                    = vk::ImageViewType::e2D;
	depth_stencil_view.format                      = framebuffer_depth_format;
	depth_stencil_view.subresourceRange            = {};
	depth_stencil_view.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
	depth_stencil_view.subresourceRange.levelCount = 1;
	depth_stencil_view.subresourceRange.layerCount = 1;
	depth_stencil_view.image                       = offscreen_pass.depth.image;
	offscreen_pass.depth.view                      = get_device().get_handle().createImageView(depth_stencil_view);

	// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering

	std::array<vk::AttachmentDescription, 2> attachment_descriptions = {};
	// Color attachment
	attachment_descriptions[0].format         = FB_COLOR_FORMAT;
	attachment_descriptions[0].samples        = vk::SampleCountFlagBits::e1;
	attachment_descriptions[0].loadOp         = vk::AttachmentLoadOp::eClear;
	attachment_descriptions[0].storeOp        = vk::AttachmentStoreOp::eStore;
	attachment_descriptions[0].stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
	attachment_descriptions[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachment_descriptions[0].initialLayout  = vk::ImageLayout::eUndefined;
	attachment_descriptions[0].finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;
	// Depth attachment
	attachment_descriptions[1].format         = framebuffer_depth_format;
	attachment_descriptions[1].samples        = vk::SampleCountFlagBits::e1;
	attachment_descriptions[1].loadOp         = vk::AttachmentLoadOp::eClear;
	attachment_descriptions[1].storeOp        = vk::AttachmentStoreOp::eDontCare;
	attachment_descriptions[1].stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
	attachment_descriptions[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachment_descriptions[1].initialLayout  = vk::ImageLayout::eUndefined;
	attachment_descriptions[1].finalLayout    = vk::ImageLayout::eDepthStencilAttachmentOptimal;

	vk::AttachmentReference color_reference = {0, vk::ImageLayout::eColorAttachmentOptimal};
	vk::AttachmentReference depth_reference = {1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

	vk::SubpassDescription subpass_description  = {};
	subpass_description.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
	subpass_description.colorAttachmentCount    = 1;
	subpass_description.pColorAttachments       = &color_reference;
	subpass_description.pDepthStencilAttachment = &depth_reference;

	// Use subpass dependencies for layout transitions
	std::array<vk::SubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass      = 0;
	dependencies[0].srcStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
	dependencies[0].dstStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask   = vk::AccessFlagBits::eShaderRead;
	dependencies[0].dstAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	dependencies[1].srcSubpass      = 0;
	dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].dstStageMask    = vk::PipelineStageFlagBits::eFragmentShader;
	dependencies[1].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstAccessMask   = vk::AccessFlagBits::eShaderRead;
	dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

	// Create the actual renderpass
	vk::RenderPassCreateInfo render_pass_create_info;

	render_pass_create_info.attachmentCount = static_cast<uint32_t>(attachment_descriptions.size());
	render_pass_create_info.pAttachments    = attachment_descriptions.data();
	render_pass_create_info.subpassCount    = 1;
	render_pass_create_info.pSubpasses      = &subpass_description;
	render_pass_create_info.dependencyCount = static_cast<uint32_t>(dependencies.size());
	render_pass_create_info.pDependencies   = dependencies.data();

	offscreen_pass.render_pass = get_device().get_handle().createRenderPass(render_pass_create_info);

	vk::ImageView attachments[2];
	attachments[0] = offscreen_pass.color.view;
	attachments[1] = offscreen_pass.depth.view;

	vk::FramebufferCreateInfo framebuffer_create_info = vkb::initializers::framebuffer_create_info();
	framebuffer_create_info.renderPass                = offscreen_pass.render_pass;
	framebuffer_create_info.attachmentCount           = 2;
	framebuffer_create_info.pAttachments              = attachments;
	framebuffer_create_info.width                     = offscreen_pass.width;
	framebuffer_create_info.height                    = offscreen_pass.height;
	framebuffer_create_info.layers                    = 1;

	offscreen_pass.framebuffer = get_device().get_handle().createFramebuffer(framebuffer_create_info);

	// Fill a descriptor for later use in a descriptor set
	offscreen_pass.descriptor.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	offscreen_pass.descriptor.imageView   = offscreen_pass.color.view;
	offscreen_pass.descriptor.sampler     = offscreen_pass.sampler;
}

void ConservativeRasterization::build_command_buffers()
{
	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	for (int32_t i = 0; i < draw_cmd_buffers.size(); ++i)
	{
		draw_cmd_buffers[i].begin(command_buffer_begin_info);

		// First render pass: Render a low res triangle to an offscreen framebuffer to use for visualization in second pass
		{
			vk::ClearValue clear_values[2];
			clear_values[0].color        = std::array<float, 4>{0.25f, 0.25f, 0.25f, 0.0f};
			clear_values[1].depthStencil = {0.0f, 0};

			vk::RenderPassBeginInfo render_pass_begin_info  = vkb::initializers::render_pass_begin_info();
			render_pass_begin_info.renderPass               = offscreen_pass.render_pass;
			render_pass_begin_info.framebuffer              = offscreen_pass.framebuffer;
			render_pass_begin_info.renderArea.extent.width  = offscreen_pass.width;
			render_pass_begin_info.renderArea.extent.height = offscreen_pass.height;
			render_pass_begin_info.clearValueCount          = 2;
			render_pass_begin_info.pClearValues             = clear_values;

			vk::Viewport viewport = vkb::initializers::viewport((float) offscreen_pass.width, (float) offscreen_pass.height, 0.0f, 1.0f);
			draw_cmd_buffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkb::initializers::rect2D(offscreen_pass.width, offscreen_pass.height, 0, 0);
			draw_cmd_buffers[i].setScissor(0, scissor);

			draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

			draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.scene, 0, 1, &descriptor_sets.scene, 0, nullptr);
			draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, conservative_raster_enabled ? pipelines.triangle_conservative_raster : pipelines.triangle);

			draw_cmd_buffers[i].bindVertexBuffers(0, triangle.vertices->get_handle(), {0});
			draw_cmd_buffers[i].bindIndexBuffer(triangle.indices->get_handle(), 0, vk::IndexType::eUint32);
			draw_cmd_buffers[i].setViewport(0, viewport);
			draw_cmd_buffers[i].drawIndexed(triangle.index_count, 1, 0, 0, 0);

			draw_cmd_buffers[i].endRenderPass();
		}

		// Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies

		// Second render pass: Render scene with conservative rasterization
		{
			vk::ClearValue clear_values[2];
			clear_values[0].color        = std::array<float, 4>{0.25f, 0.25f, 0.25f, 0.25f};
			clear_values[1].depthStencil = {0.0f, 0};

			vk::RenderPassBeginInfo render_pass_begin_info  = vkb::initializers::render_pass_begin_info();
			render_pass_begin_info.framebuffer              = framebuffers[i];
			render_pass_begin_info.renderPass               = render_pass;
			render_pass_begin_info.renderArea.offset.x      = 0;
			render_pass_begin_info.renderArea.offset.y      = 0;
			render_pass_begin_info.renderArea.extent.width  = width;
			render_pass_begin_info.renderArea.extent.height = height;
			render_pass_begin_info.clearValueCount          = 2;
			render_pass_begin_info.pClearValues             = clear_values;

			draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);
			vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
			draw_cmd_buffers[i].setViewport(0, viewport);
			vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
			draw_cmd_buffers[i].setScissor(0, scissor);

			// Low-res triangle from offscreen framebuffer
			draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.fullscreen);
			draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.fullscreen, 0, 1, &descriptor_sets.fullscreen, 0, nullptr);
			draw_cmd_buffers[i].draw(3, 1, 0, 0);

			// Overlay actual triangle
			draw_cmd_buffers[i].bindVertexBuffers(0, triangle.vertices->get_handle(), {0});
			draw_cmd_buffers[i].bindIndexBuffer(triangle.indices->get_handle(), 0, vk::IndexType::eUint32);
			draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.triangle_overlay);
			draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.scene, 0, 1, &descriptor_sets.scene, 0, nullptr);
			draw_cmd_buffers[i].draw(3, 1, 0, 0);

			draw_ui(draw_cmd_buffers[i]);

			draw_cmd_buffers[i].endRenderPass();
		}

		draw_cmd_buffers[i].end();
	}
}

void ConservativeRasterization::load_assets()
{
	// Create a single triangle
	struct Vertex
	{
		float position[3];
		float color[3];
	};

	std::vector<Vertex> vertex_buffer{
	    {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
	    {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	};

    std::vector<uint32_t> index_buffer{{
	    0,
	    1,
	    2,
	}};

	triangle.index_count = static_cast<uint32_t>(index_buffer.size());

	// Host visible source buffers (staging)
    triangle.vertices = std::make_unique<vkb::core::Buffer>(
        get_device().stage_to_device_buffer(vertex_buffer, vk::BufferUsageFlagBits::eVertexBuffer));
    triangle.indices = std::make_unique<vkb::core::Buffer>(
        get_device().stage_to_device_buffer(index_buffer, vk::BufferUsageFlagBits::eIndexBuffer));
}

void ConservativeRasterization::setup_descriptor_pool()
{
	std::vector<vk::DescriptorPoolSize> pool_sizes = {
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 3),
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 2)};
	vk::DescriptorPoolCreateInfo descriptor_pool_info =
	    vkb::initializers::descriptor_pool_create_info(pool_sizes, 2);
	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_info);
}

void ConservativeRasterization::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings;
	vk::DescriptorSetLayoutCreateInfo           descriptor_layout;
	vk::PipelineLayoutCreateInfo                pipeline_layout_create_info;

	// Scene rendering
	set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0),                 // Binding 0: Vertex shader uniform buffer
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1),        // Binding 1: Fragment shader image sampler
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 2)                // Binding 2: Fragment shader uniform buffer
	};
	descriptor_layout            = vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	descriptor_set_layouts.scene = get_device().get_handle().createDescriptorSetLayout(descriptor_layout);
	pipeline_layout_create_info  = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layouts.scene, 1);
	pipeline_layouts.scene       = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);

	// Fullscreen pass
	set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0),                // Binding 0: Vertex shader uniform buffer
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1)        // Binding 1: Fragment shader image sampler
	};
	descriptor_layout                 = vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	descriptor_set_layouts.fullscreen = get_device().get_handle().createDescriptorSetLayout(descriptor_layout);
	pipeline_layout_create_info       = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layouts.fullscreen, 1);
	pipeline_layouts.fullscreen       = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);
}

void ConservativeRasterization::setup_descriptor_set()
{
	vk::DescriptorSetAllocateInfo descriptor_set_allocate_info;

	// Scene rendering
	descriptor_set_allocate_info                                        = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layouts.scene, 1);
	descriptor_sets.scene                                               = get_device().get_handle().allocateDescriptorSets(descriptor_set_allocate_info)[0];
	vk::DescriptorBufferInfo            scene_buffer_descriptor         = create_descriptor(*uniform_buffers.scene);
	std::vector<vk::WriteDescriptorSet> offscreen_write_descriptor_sets = {
	    vkb::initializers::write_descriptor_set(descriptor_sets.scene, vk::DescriptorType::eUniformBuffer, 0, &scene_buffer_descriptor),
	};
	get_device().get_handle().updateDescriptorSets(offscreen_write_descriptor_sets, nullptr);

	// Fullscreen pass
	descriptor_set_allocate_info                              = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layouts.fullscreen, 1);
	descriptor_sets.fullscreen                                = get_device().get_handle().allocateDescriptorSets(descriptor_set_allocate_info)[0];
	std::vector<vk::WriteDescriptorSet> write_descriptor_sets = {
	    vkb::initializers::write_descriptor_set(descriptor_sets.fullscreen, vk::DescriptorType::eCombinedImageSampler, 1, &offscreen_pass.descriptor),
	};
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void ConservativeRasterization::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info();

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eClockwise);

	vk::PipelineColorBlendAttachmentState blend_attachment_state =
	    vkb::initializers::pipeline_color_blend_attachment_state();

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(1, &blend_attachment_state);

	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info();

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

	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages;

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(pipeline_layouts.fullscreen, render_pass);

	// Conservative rasterization setup

	// Get device properties for conservative rasterization
	// Requires VK_KHR_get_physical_device_properties2 and manual function pointer creation
	auto structure_chain           = get_device().get_physical_device().getProperties2KHR<vk::PhysicalDeviceProperties2KHR, vk::PhysicalDeviceConservativeRasterizationPropertiesEXT>();
	conservative_raster_properties = structure_chain.get<vk::PhysicalDeviceConservativeRasterizationPropertiesEXT>();

	// Vertex bindings and attributes
	std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex)),
	};
	std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, 0),                        // Location 0: Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3),        // Location 1: Color
	};
	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;
	pipeline_create_info.stageCount          = vkb::to_u32(shader_stages.size());
	pipeline_create_info.pStages             = shader_stages.data();

	// Full screen pass
	shader_stages[0] = load_shader("conservative_rasterization/fullscreen.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("conservative_rasterization/fullscreen.frag", vk::ShaderStageFlagBits::eFragment);
	// Empty vertex input state (full screen triangle generated in vertex shader)
	vk::PipelineVertexInputStateCreateInfo empty_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	pipeline_create_info.pVertexInputState                   = &empty_input_state;
	pipeline_create_info.layout                              = pipeline_layouts.fullscreen;
	pipelines.fullscreen                                     = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	pipeline_create_info.pVertexInputState = &vertex_input_state;
	pipeline_create_info.layout            = pipeline_layouts.scene;

	// Original triangle outline
	// TODO: Check support for lines
	rasterization_state.lineWidth   = 2.0f;
	rasterization_state.polygonMode = vk::PolygonMode::eLine;
	shader_stages[0]                = load_shader("conservative_rasterization/triangle.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1]                = load_shader("conservative_rasterization/triangleoverlay.frag", vk::ShaderStageFlagBits::eFragment);
	pipelines.triangle_overlay      = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	pipeline_create_info.renderPass = offscreen_pass.render_pass;

	// Triangle rendering
	rasterization_state.polygonMode = vk::PolygonMode::eFill;
	shader_stages[0]                = load_shader("conservative_rasterization/triangle.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1]                = load_shader("conservative_rasterization/triangle.frag", vk::ShaderStageFlagBits::eFragment);

	// Basic pipeline
	pipelines.triangle = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Pipeline with conservative rasterization enabled
	vk::PipelineRasterizationConservativeStateCreateInfoEXT conservative_rasterization_state;
	conservative_rasterization_state.conservativeRasterizationMode    = vk::ConservativeRasterizationModeEXT::eOverestimate;
	conservative_rasterization_state.extraPrimitiveOverestimationSize = conservative_raster_properties.maxExtraPrimitiveOverestimationSize;

	// Conservative rasterization state has to be chained into the pipeline rasterization state create info structure
	rasterization_state.pNext = &conservative_rasterization_state;

	pipelines.triangle_conservative_raster = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

// Prepare and initialize uniform buffer containing shader uniforms
void ConservativeRasterization::prepare_uniform_buffers()
{
	uniform_buffers.scene = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                            sizeof(ubo_scene),
	                                                            vk::BufferUsageFlagBits::eUniformBuffer,
	                                                            vma::MemoryUsage::eCpuToGpu);

	update_uniform_buffers_scene();
}

void ConservativeRasterization::update_uniform_buffers_scene()
{
	ubo_scene.projection = camera.matrices.perspective;
	ubo_scene.model      = camera.matrices.view;
	uniform_buffers.scene->convert_and_update(ubo_scene);
}

void ConservativeRasterization::draw()
{
	ApiVulkanSample::prepare_frame();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];
	queue.submit(submit_info, nullptr);
	ApiVulkanSample::submit_frame();
}

bool ConservativeRasterization::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.type = vkb::CameraType::LookAt;
	camera.set_perspective(60.0f, (float) width / (float) height, 512.0f, 0.1f);
	camera.set_rotation(glm::vec3(0.0f));
	camera.set_translation(glm::vec3(0.0f, 0.0f, -2.0f));

	load_assets();
	prepare_offscreen();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_set();
	build_command_buffers();
	prepared = true;
	return true;
}

void ConservativeRasterization::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
	if (camera.updated)
		update_uniform_buffers_scene();
}

void ConservativeRasterization::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Settings"))
	{
		if (drawer.checkbox("Conservative rasterization", &conservative_raster_enabled))
		{
			build_command_buffers();
		}
	}
	if (drawer.header("Device properties"))
	{
		drawer.text("maxExtraPrimitiveOverestimationSize: %f", conservative_raster_properties.maxExtraPrimitiveOverestimationSize);
		drawer.text("extraPrimitiveOverestimationSizeGranularity: %f", conservative_raster_properties.extraPrimitiveOverestimationSizeGranularity);
		drawer.text("primitiveUnderestimation:  %s", conservative_raster_properties.primitiveUnderestimation ? "yes" : "no");
		drawer.text("conservativePointAndLineRasterization:  %s", conservative_raster_properties.conservativePointAndLineRasterization ? "yes" : "no");
		drawer.text("degenerateTrianglesRasterized: %s", conservative_raster_properties.degenerateTrianglesRasterized ? "yes" : "no");
		drawer.text("degenerateLinesRasterized: %s", conservative_raster_properties.degenerateLinesRasterized ? "yes" : "no");
		drawer.text("fullyCoveredFragmentShaderInputVariable: %s", conservative_raster_properties.fullyCoveredFragmentShaderInputVariable ? "yes" : "no");
		drawer.text("conservativeRasterizationPostDepthCoverage: %s", conservative_raster_properties.conservativeRasterizationPostDepthCoverage ? "yes" : "no");
	}
}

std::unique_ptr<vkb::VulkanSample> create_conservative_rasterization()
{
	return std::make_unique<ConservativeRasterization>();
}
