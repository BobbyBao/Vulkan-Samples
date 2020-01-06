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
 * High dynamic range rendering
 */

#include "hdr.h"

#include "scene_graph/components/sub_mesh.h"

HDR::HDR()
{
	title = "High dynamic range rendering";
}

HDR::~HDR()
{
	if (device)
	{
		get_device().get_handle().destroy(pipelines.skybox);
		get_device().get_handle().destroy(pipelines.reflect);
		get_device().get_handle().destroy(pipelines.composition);
		get_device().get_handle().destroy(pipelines.bloom[0]);
		get_device().get_handle().destroy(pipelines.bloom[1]);

		get_device().get_handle().destroy(pipeline_layouts.models);
		get_device().get_handle().destroy(pipeline_layouts.composition);
		get_device().get_handle().destroy(pipeline_layouts.bloom_filter);

		get_device().get_handle().destroy(descriptor_set_layouts.models);
		get_device().get_handle().destroy(descriptor_set_layouts.composition);
		get_device().get_handle().destroy(descriptor_set_layouts.bloom_filter);

		get_device().get_handle().destroy(offscreen.render_pass);
		get_device().get_handle().destroy(filter_pass.render_pass);

		get_device().get_handle().destroy(offscreen.framebuffer);
		get_device().get_handle().destroy(filter_pass.framebuffer);

		get_device().get_handle().destroy(offscreen.sampler);
		get_device().get_handle().destroy(filter_pass.sampler);

		offscreen.depth.destroy(get_device().get_handle());
		offscreen.color[0].destroy(get_device().get_handle());
		offscreen.color[1].destroy(get_device().get_handle());

		filter_pass.color[0].destroy(get_device().get_handle());

		get_device().get_handle().destroy(textures.envmap.sampler);
	}
}

void HDR::get_device_features()
{
	// Enable anisotropic filtering if supported
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	}
}

void HDR::build_command_buffers()
{
	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	vk::ClearValue clear_values[2];
	clear_values[0].color        = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
	clear_values[1].depthStencil = {0.0f, 0};

	vk::RenderPassBeginInfo render_pass_begin_info = vkb::initializers::render_pass_begin_info();
	render_pass_begin_info.renderPass              = render_pass;
	render_pass_begin_info.renderArea.offset.x     = 0;
	render_pass_begin_info.renderArea.offset.y     = 0;
	render_pass_begin_info.clearValueCount         = 2;
	render_pass_begin_info.pClearValues            = clear_values;

	for (int32_t i = 0; i < draw_cmd_buffers.size(); ++i)
	{
		draw_cmd_buffers[i].begin(command_buffer_begin_info);

		{
			/*
				First pass: Render scene to offscreen framebuffer
			*/

			std::array<vk::ClearValue, 3> clear_values;
			clear_values[0].color        = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
			clear_values[1].color        = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
			clear_values[2].depthStencil = {0.0f, 0};

			vk::RenderPassBeginInfo render_pass_begin_info  = vkb::initializers::render_pass_begin_info();
			render_pass_begin_info.renderPass               = offscreen.render_pass;
			render_pass_begin_info.framebuffer              = offscreen.framebuffer;
			render_pass_begin_info.renderArea.extent.width  = offscreen.width;
			render_pass_begin_info.renderArea.extent.height = offscreen.height;
			render_pass_begin_info.clearValueCount          = 3;
			render_pass_begin_info.pClearValues             = clear_values.data();

			draw_cmd_buffers[i].beginRenderPass(&render_pass_begin_info, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkb::initializers::viewport((float) offscreen.width, (float) offscreen.height, 0.0f, 1.0f);
			draw_cmd_buffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkb::initializers::rect2D(offscreen.width, offscreen.height, 0, 0);
			draw_cmd_buffers[i].setScissor(0, scissor);

			vk::DeviceSize offsets[1] = {0};

			// Skybox
			if (display_skybox)
			{
				draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skybox);
				draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.models, 0, descriptor_sets.skybox, nullptr);

				draw_model(models.skybox, draw_cmd_buffers[i]);
			}

			// 3D object
			draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.reflect);
			draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.models, 0, descriptor_sets.object, nullptr);

			draw_model(models.objects[models.object_index], draw_cmd_buffers[i]);

			draw_cmd_buffers[i].endRenderPass();
		}

		/*
			Second render pass: First bloom pass
		*/
		if (bloom)
		{
			vk::ClearValue clear_values[2];
			clear_values[0].color        = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
			clear_values[1].depthStencil = {0.0f, 0};

			// Bloom filter
			vk::RenderPassBeginInfo render_pass_begin_info  = vkb::initializers::render_pass_begin_info();
			render_pass_begin_info.framebuffer              = filter_pass.framebuffer;
			render_pass_begin_info.renderPass               = filter_pass.render_pass;
			render_pass_begin_info.clearValueCount          = 1;
			render_pass_begin_info.renderArea.extent.width  = filter_pass.width;
			render_pass_begin_info.renderArea.extent.height = filter_pass.height;
			render_pass_begin_info.pClearValues             = clear_values;

			draw_cmd_buffers[i].beginRenderPass(&render_pass_begin_info, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkb::initializers::viewport((float) filter_pass.width, (float) filter_pass.height, 0.0f, 1.0f);
			draw_cmd_buffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkb::initializers::rect2D(filter_pass.width, filter_pass.height, 0, 0);
			draw_cmd_buffers[i].setScissor(0, scissor);

			draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.bloom_filter, 0, 1, &descriptor_sets.bloom_filter, 0, NULL);

			draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.bloom[1]);
			draw_cmd_buffers[i].draw(3, 1, 0, 0);

			draw_cmd_buffers[i].endRenderPass();
		}

		/*
			Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
		*/

		/*
			Third render pass: Scene rendering with applied second bloom pass (when enabled)
		*/
		{
			vk::ClearValue clear_values[2];
			clear_values[0].color        = std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f};
			clear_values[1].depthStencil = {0.0f, 0};

			// Final composition
			vk::RenderPassBeginInfo render_pass_begin_info  = vkb::initializers::render_pass_begin_info();
			render_pass_begin_info.framebuffer              = framebuffers[i];
			render_pass_begin_info.renderPass               = render_pass;
			render_pass_begin_info.clearValueCount          = 2;
			render_pass_begin_info.renderArea.extent.width  = width;
			render_pass_begin_info.renderArea.extent.height = height;
			render_pass_begin_info.pClearValues             = clear_values;

			draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

			vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
			draw_cmd_buffers[i].setViewport(0, viewport);

			vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
			draw_cmd_buffers[i].setScissor(0, scissor);

			draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.composition, 0, descriptor_sets.composition, nullptr);

			// Scene
			draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.composition);
			draw_cmd_buffers[i].draw(3, 1, 0, 0);

			// Bloom
			if (bloom)
			{
				draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.bloom[0]);
				draw_cmd_buffers[i].draw(3, 1, 0, 0);
			}

			draw_ui(draw_cmd_buffers[i]);

			draw_cmd_buffers[i].endRenderPass();
		}

		draw_cmd_buffers[i].end();
	}
}

void HDR::create_attachment(vk::Format format, vk::ImageUsageFlagBits usage, FrameBufferAttachment *attachment)
{
	vk::ImageAspectFlags aspect_mask;
	vk::ImageLayout      image_layout;

	attachment->format = format;

	if (usage & vk::ImageUsageFlagBits::eColorAttachment)
	{
		aspect_mask  = vk::ImageAspectFlagBits::eColor;
		image_layout = vk::ImageLayout::eColorAttachmentOptimal;
	}
	if (usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)
	{
		aspect_mask = vk::ImageAspectFlagBits::eDepth;
		// Stencil aspect should only be set on depth + stencil formats (vk::Format::eD16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8Uint
		if (format >= vk::Format::eD16UnormS8Uint)
		{
			aspect_mask |= vk::ImageAspectFlagBits::eStencil;
		}
		image_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	}

	assert(aspect_mask.operator VkImageAspectFlags() > 0);

	vk::ImageCreateInfo image = vkb::initializers::image_create_info();
	image.imageType           = vk::ImageType::e2D;
	image.format              = format;
	image.extent.width        = offscreen.width;
	image.extent.height       = offscreen.height;
	image.extent.depth        = 1;
	image.mipLevels           = 1;
	image.arrayLayers         = 1;
	image.samples             = vk::SampleCountFlagBits::e1;
	image.tiling              = vk::ImageTiling::eOptimal;
	image.usage               = usage | vk::ImageUsageFlagBits::eSampled;

	vk::MemoryAllocateInfo memory_allocate_info = vkb::initializers::memory_allocate_info();
	vk::MemoryRequirements memory_requirements;

	attachment->image                    = get_device().get_handle().createImage(image);
	memory_requirements                  = get_device().get_handle().getImageMemoryRequirements(attachment->image);
	memory_allocate_info.allocationSize  = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	attachment->mem                      = get_device().get_handle().allocateMemory(memory_allocate_info);
	get_device().get_handle().bindImageMemory(attachment->image, attachment->mem, 0);

	vk::ImageViewCreateInfo image_view_create_info;
	image_view_create_info.viewType                        = vk::ImageViewType::e2D;
	image_view_create_info.format                          = format;
	image_view_create_info.subresourceRange                = {};
	image_view_create_info.subresourceRange.aspectMask     = aspect_mask;
	image_view_create_info.subresourceRange.baseMipLevel   = 0;
	image_view_create_info.subresourceRange.levelCount     = 1;
	image_view_create_info.subresourceRange.baseArrayLayer = 0;
	image_view_create_info.subresourceRange.layerCount     = 1;
	image_view_create_info.image                           = attachment->image;
	attachment->view                                       = get_device().get_handle().createImageView(image_view_create_info);
}

// Prepare a new framebuffer and attachments for offscreen rendering (G-Buffer)
void HDR::prepare_offscreen_buffer()
{
	{
		offscreen.width  = width;
		offscreen.height = height;

		// Color attachments

		// We are using two 128-Bit RGBA floating point color buffers for this sample
		// In a performance or bandwith-limited scenario you should consider using a format with lower precision
		create_attachment(vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment, &offscreen.color[0]);
		create_attachment(vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment, &offscreen.color[1]);
		// Depth attachment
		create_attachment(depth_format, vk::ImageUsageFlagBits::eDepthStencilAttachment, &offscreen.depth);

		// Set up separate renderpass with references to the colorand depth attachments
		std::array<vk::AttachmentDescription, 3> attachment_descriptions = {};

		// Init attachment properties
		for (uint32_t i = 0; i < 3; ++i)
		{
			attachment_descriptions[i].samples        = vk::SampleCountFlagBits::e1;
			attachment_descriptions[i].loadOp         = vk::AttachmentLoadOp::eClear;
			attachment_descriptions[i].storeOp        = vk::AttachmentStoreOp::eStore;
			attachment_descriptions[i].stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
			attachment_descriptions[i].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
			if (i == 2)
			{
				attachment_descriptions[i].initialLayout = vk::ImageLayout::eUndefined;
				attachment_descriptions[i].finalLayout   = vk::ImageLayout::eDepthStencilAttachmentOptimal;
			}
			else
			{
				attachment_descriptions[i].initialLayout = vk::ImageLayout::eUndefined;
				attachment_descriptions[i].finalLayout   = vk::ImageLayout::eShaderReadOnlyOptimal;
			}
		}

		// Formats
		attachment_descriptions[0].format = offscreen.color[0].format;
		attachment_descriptions[1].format = offscreen.color[1].format;
		attachment_descriptions[2].format = offscreen.depth.format;

		std::vector<vk::AttachmentReference> color_references;
		color_references.push_back({0, vk::ImageLayout::eColorAttachmentOptimal});
		color_references.push_back({1, vk::ImageLayout::eColorAttachmentOptimal});

		vk::AttachmentReference depth_reference = {};
		depth_reference.attachment              = 2;
		depth_reference.layout                  = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass  = {};
		subpass.pipelineBindPoint       = vk::PipelineBindPoint::eGraphics;
		subpass.pColorAttachments       = color_references.data();
		subpass.colorAttachmentCount    = 2;
		subpass.pDepthStencilAttachment = &depth_reference;

		// Use subpass dependencies for attachment layput transitions
		std::array<vk::SubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass      = 0;
		dependencies[0].srcStageMask    = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask   = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask   = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		dependencies[1].srcSubpass      = 0;
		dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask    = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask   = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		vk::RenderPassCreateInfo render_pass_create_info;
		render_pass_create_info.pAttachments    = attachment_descriptions.data();
		render_pass_create_info.attachmentCount = static_cast<uint32_t>(attachment_descriptions.size());
		render_pass_create_info.subpassCount    = 1;
		render_pass_create_info.pSubpasses      = &subpass;
		render_pass_create_info.dependencyCount = 2;
		render_pass_create_info.pDependencies   = dependencies.data();

		offscreen.render_pass = get_device().get_handle().createRenderPass(render_pass_create_info);

		std::array<vk::ImageView, 3> attachments;
		attachments[0] = offscreen.color[0].view;
		attachments[1] = offscreen.color[1].view;
		attachments[2] = offscreen.depth.view;

		vk::FramebufferCreateInfo framebuffer_create_info = {};
		framebuffer_create_info.pNext                     = NULL;
		framebuffer_create_info.renderPass                = offscreen.render_pass;
		framebuffer_create_info.pAttachments              = attachments.data();
		framebuffer_create_info.attachmentCount           = static_cast<uint32_t>(attachments.size());
		framebuffer_create_info.width                     = offscreen.width;
		framebuffer_create_info.height                    = offscreen.height;
		framebuffer_create_info.layers                    = 1;

		offscreen.framebuffer = get_device().get_handle().createFramebuffer(framebuffer_create_info);

		// Create sampler to sample from the color attachments
		vk::SamplerCreateInfo sampler = vkb::initializers::sampler_create_info();
		sampler.magFilter             = vk::Filter::eNearest;
		sampler.minFilter             = vk::Filter::eNearest;
		sampler.mipmapMode            = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU          = vk::SamplerAddressMode::eClampToEdge;
		sampler.addressModeV          = sampler.addressModeU;
		sampler.addressModeW          = sampler.addressModeU;
		sampler.mipLodBias            = 0.0f;
		sampler.maxAnisotropy         = 1.0f;
		sampler.minLod                = 0.0f;
		sampler.maxLod                = 1.0f;
		sampler.borderColor           = vk::BorderColor::eFloatOpaqueWhite;

		offscreen.sampler = get_device().get_handle().createSampler(sampler);
	}

	// Bloom separable filter pass
	{
		filter_pass.width  = width;
		filter_pass.height = height;

		// Color attachments

		// Two floating point color buffers
		create_attachment(vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment, &filter_pass.color[0]);

		// Set up separate renderpass with references to the colorand depth attachments
		std::array<vk::AttachmentDescription, 1> attachment_descriptions = {};

		// Init attachment properties
		attachment_descriptions[0].samples        = vk::SampleCountFlagBits::e1;
		attachment_descriptions[0].loadOp         = vk::AttachmentLoadOp::eClear;
		attachment_descriptions[0].storeOp        = vk::AttachmentStoreOp::eStore;
		attachment_descriptions[0].stencilLoadOp  = vk::AttachmentLoadOp::eDontCare;
		attachment_descriptions[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachment_descriptions[0].initialLayout  = vk::ImageLayout::eUndefined;
		attachment_descriptions[0].finalLayout    = vk::ImageLayout::eShaderReadOnlyOptimal;
		attachment_descriptions[0].format         = filter_pass.color[0].format;

		std::vector<vk::AttachmentReference> color_references;
		color_references.push_back({0, vk::ImageLayout::eColorAttachmentOptimal});

		vk::SubpassDescription subpass = {};
		subpass.pipelineBindPoint      = vk::PipelineBindPoint::eGraphics;
		subpass.pColorAttachments      = color_references.data();
		subpass.colorAttachmentCount   = 1;

		// Use subpass dependencies for attachment layput transitions
		std::array<vk::SubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass      = 0;
		dependencies[0].srcStageMask    = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[0].dstStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[0].srcAccessMask   = vk::AccessFlagBits::eMemoryRead;
		dependencies[0].dstAccessMask   = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[0].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		dependencies[1].srcSubpass      = 0;
		dependencies[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask    = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dependencies[1].dstStageMask    = vk::PipelineStageFlagBits::eBottomOfPipe;
		dependencies[1].srcAccessMask   = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		dependencies[1].dstAccessMask   = vk::AccessFlagBits::eMemoryRead;
		dependencies[1].dependencyFlags = vk::DependencyFlagBits::eByRegion;

		vk::RenderPassCreateInfo render_pass_create_info = {};
		render_pass_create_info.pAttachments             = attachment_descriptions.data();
		render_pass_create_info.attachmentCount          = static_cast<uint32_t>(attachment_descriptions.size());
		render_pass_create_info.subpassCount             = 1;
		render_pass_create_info.pSubpasses               = &subpass;
		render_pass_create_info.dependencyCount          = 2;
		render_pass_create_info.pDependencies            = dependencies.data();

		filter_pass.render_pass = get_device().get_handle().createRenderPass(render_pass_create_info);

		std::array<vk::ImageView, 1> attachments;
		attachments[0] = filter_pass.color[0].view;

		vk::FramebufferCreateInfo framebuffer_create_info = {};
		framebuffer_create_info.pNext                     = NULL;
		framebuffer_create_info.renderPass                = filter_pass.render_pass;
		framebuffer_create_info.pAttachments              = attachments.data();
		framebuffer_create_info.attachmentCount           = static_cast<uint32_t>(attachments.size());
		framebuffer_create_info.width                     = filter_pass.width;
		framebuffer_create_info.height                    = filter_pass.height;
		framebuffer_create_info.layers                    = 1;

		filter_pass.framebuffer = get_device().get_handle().createFramebuffer(framebuffer_create_info);

		// Create sampler to sample from the color attachments
		vk::SamplerCreateInfo sampler = vkb::initializers::sampler_create_info();
		sampler.magFilter             = vk::Filter::eNearest;
		sampler.minFilter             = vk::Filter::eNearest;
		sampler.mipmapMode            = vk::SamplerMipmapMode::eLinear;
		sampler.addressModeU          = vk::SamplerAddressMode::eClampToEdge;
		sampler.addressModeV          = sampler.addressModeU;
		sampler.addressModeW          = sampler.addressModeU;
		sampler.mipLodBias            = 0.0f;
		sampler.maxAnisotropy         = 1.0f;
		sampler.minLod                = 0.0f;
		sampler.maxLod                = 1.0f;
		sampler.borderColor           = vk::BorderColor::eFloatOpaqueWhite;

		filter_pass.sampler = get_device().get_handle().createSampler(sampler);
	}
}

void HDR::load_assets()
{
	// Models
	models.skybox                      = load_model("scenes/cube.gltf");
	std::vector<std::string> filenames = {"geosphere.gltf", "teapot.gltf", "torusknot.gltf"};
	object_names                       = {"Sphere", "Teapot", "Torusknot"};
	for (auto file : filenames)
	{
		auto object = load_model("scenes/" + file);
		models.objects.emplace_back(std::move(object));
	}

	// Transforms
	auto geosphere_matrix = glm::mat4(1.0f);
	auto teapot_matrix    = glm::mat4(1.0f);
	teapot_matrix         = glm::scale(teapot_matrix, glm::vec3(10.0f, 10.0f, 10.0f));
	teapot_matrix         = glm::rotate(teapot_matrix, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	auto torus_matrix     = glm::mat4(1.0f);
	models.transforms.push_back(geosphere_matrix);
	models.transforms.push_back(teapot_matrix);
	models.transforms.push_back(torus_matrix);

	// Load HDR cube map
	textures.envmap = load_texture_cubemap("textures/uffizi_rgba16f_cube.ktx");
}

void HDR::setup_descriptor_pool()
{
	std::vector<vk::DescriptorPoolSize> pool_sizes = {
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 4),
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 6)};

	uint32_t num_descriptor_sets = 4;

	vk::DescriptorPoolCreateInfo descriptor_pool_create_info =
	    vkb::initializers::descriptor_pool_create_info(pool_sizes, num_descriptor_sets);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);
}

void HDR::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 0),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eFragment, 2),
	};

	vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info =
	    vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));

	descriptor_set_layouts.models = get_device().get_handle().createDescriptorSetLayout(descriptor_layout_create_info);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info =
	    vkb::initializers::pipeline_layout_create_info(
	        &descriptor_set_layouts.models,
	        1);

	pipeline_layouts.models = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);

	// Bloom filter
	set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1),
	};

	descriptor_layout_create_info       = vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	descriptor_set_layouts.bloom_filter = get_device().get_handle().createDescriptorSetLayout(descriptor_layout_create_info);

	pipeline_layout_create_info   = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layouts.bloom_filter, 1);
	pipeline_layouts.bloom_filter = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);

	// G-Buffer composition
	set_layout_bindings = {
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 0),
	    vkb::initializers::descriptor_set_layout_binding(vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1),
	};

	descriptor_layout_create_info      = vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	descriptor_set_layouts.composition = get_device().get_handle().createDescriptorSetLayout(descriptor_layout_create_info);

	pipeline_layout_create_info  = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layouts.composition, 1);
	pipeline_layouts.composition = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);
}

void HDR::setup_descriptor_sets()
{
	vk::DescriptorSetAllocateInfo alloc_info =
	    vkb::initializers::descriptor_set_allocate_info(
	        descriptor_pool,
	        &descriptor_set_layouts.models,
	        1);

	// 3D object descriptor set
	descriptor_sets.object = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo matrix_buffer_descriptor     = create_descriptor(*uniform_buffers.matrices);
	vk::DescriptorImageInfo  environment_image_descriptor = create_descriptor(textures.envmap);
	vk::DescriptorBufferInfo params_buffer_descriptor     = create_descriptor(*uniform_buffers.params);

	// Sky box descriptor set
	descriptor_sets.skybox = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	matrix_buffer_descriptor     = create_descriptor(*uniform_buffers.matrices);
	environment_image_descriptor = create_descriptor(textures.envmap);
	params_buffer_descriptor     = create_descriptor(*uniform_buffers.params);

	// Bloom filter
	alloc_info                   = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layouts.bloom_filter, 1);
	descriptor_sets.bloom_filter = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	std::vector<vk::DescriptorImageInfo> color_descriptors1 = {
	    vkb::initializers::descriptor_image_info(offscreen.sampler, offscreen.color[0].view, vk::ImageLayout::eShaderReadOnlyOptimal),
	    vkb::initializers::descriptor_image_info(offscreen.sampler, offscreen.color[1].view, vk::ImageLayout::eShaderReadOnlyOptimal),
	};

	// Composition descriptor set
	alloc_info                  = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layouts.composition, 1);
	descriptor_sets.composition = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	std::vector<vk::DescriptorImageInfo> color_descriptors2 = {
	    vkb::initializers::descriptor_image_info(offscreen.sampler, offscreen.color[0].view, vk::ImageLayout::eShaderReadOnlyOptimal),
	    vkb::initializers::descriptor_image_info(offscreen.sampler, filter_pass.color[0].view, vk::ImageLayout::eShaderReadOnlyOptimal),
	};

	std::vector<vk::WriteDescriptorSet> write_descriptor_sets = {
	    vkb::initializers::write_descriptor_set(descriptor_sets.object, vk::DescriptorType::eUniformBuffer, 0, &matrix_buffer_descriptor),
	    vkb::initializers::write_descriptor_set(descriptor_sets.object, vk::DescriptorType::eCombinedImageSampler, 1, &environment_image_descriptor),
	    vkb::initializers::write_descriptor_set(descriptor_sets.object, vk::DescriptorType::eUniformBuffer, 2, &params_buffer_descriptor),
	    vkb::initializers::write_descriptor_set(descriptor_sets.skybox, vk::DescriptorType::eUniformBuffer, 0, &matrix_buffer_descriptor),
	    vkb::initializers::write_descriptor_set(descriptor_sets.skybox, vk::DescriptorType::eCombinedImageSampler, 1, &environment_image_descriptor),
	    vkb::initializers::write_descriptor_set(descriptor_sets.skybox, vk::DescriptorType::eUniformBuffer, 2, &params_buffer_descriptor),
	    vkb::initializers::write_descriptor_set(descriptor_sets.bloom_filter, vk::DescriptorType::eCombinedImageSampler, 0, &color_descriptors1[0]),
	    vkb::initializers::write_descriptor_set(descriptor_sets.bloom_filter, vk::DescriptorType::eCombinedImageSampler, 1, &color_descriptors1[1]),
	    vkb::initializers::write_descriptor_set(descriptor_sets.composition, vk::DescriptorType::eCombinedImageSampler, 0, &color_descriptors2[0]),
	    vkb::initializers::write_descriptor_set(descriptor_sets.composition, vk::DescriptorType::eCombinedImageSampler, 1, &color_descriptors2[1]),
	};
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void HDR::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info();

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info();

	vk::PipelineColorBlendAttachmentState blend_attachment_state =
	    vkb::initializers::pipeline_color_blend_attachment_state();

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(
	        1,
	        &blend_attachment_state);

	// Note: Using Reversed depth-buffer for increased precision, so Greater depth values are kept
	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info(
	        VK_FALSE,
	        VK_FALSE,
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

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(
	        pipeline_layouts.models,
	        render_pass);

	std::vector<vk::PipelineColorBlendAttachmentState> blend_attachment_states = {
	    vkb::initializers::pipeline_color_blend_attachment_state(),
	    vkb::initializers::pipeline_color_blend_attachment_state(),
	};

	pipeline_create_info.pInputAssemblyState = &input_assembly_state;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;

	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages;
	pipeline_create_info.stageCount = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages    = shader_stages.data();

	vk::SpecializationInfo                    specialization_info;
	std::array<vk::SpecializationMapEntry, 1> specialization_map_entries;

	// Full screen pipelines

	// Empty vertex input state, full screen triangles are generated by the vertex shader
	vk::PipelineVertexInputStateCreateInfo empty_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	pipeline_create_info.pVertexInputState                   = &empty_input_state;

	// Final fullscreen composition pass pipeline
	shader_stages[0]                  = load_shader("hdr/composition.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1]                  = load_shader("hdr/composition.frag", vk::ShaderStageFlagBits::eFragment);
	pipeline_create_info.layout       = pipeline_layouts.composition;
	pipeline_create_info.renderPass   = render_pass;
	rasterization_state.cullMode      = vk::CullModeFlagBits::eFront;
	color_blend_state.attachmentCount = 1;
	color_blend_state.pAttachments    = blend_attachment_states.data();
	pipelines.composition             = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Bloom pass
	shader_stages[0]                           = load_shader("hdr/bloom.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1]                           = load_shader("hdr/bloom.frag", vk::ShaderStageFlagBits::eFragment);
	color_blend_state.pAttachments             = &blend_attachment_state;
	blend_attachment_state.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	blend_attachment_state.blendEnable         = VK_TRUE;
	blend_attachment_state.colorBlendOp        = vk::BlendOp::eAdd;
	blend_attachment_state.srcColorBlendFactor = vk::BlendFactor::eOne;
	blend_attachment_state.dstColorBlendFactor = vk::BlendFactor::eOne;
	blend_attachment_state.alphaBlendOp        = vk::BlendOp::eAdd;
	blend_attachment_state.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha;
	blend_attachment_state.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;

	// Set constant parameters via specialization constants
	specialization_map_entries[0]        = vkb::initializers::specialization_map_entry(0, 0, sizeof(uint32_t));
	uint32_t dir                         = 1;
	specialization_info                  = vkb::initializers::specialization_info(1, specialization_map_entries.data(), sizeof(dir), &dir);
	shader_stages[1].pSpecializationInfo = &specialization_info;

	pipelines.bloom[0] = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Second blur pass (into separate framebuffer)
	pipeline_create_info.renderPass = filter_pass.render_pass;
	dir                             = 0;
	pipelines.bloom[1]              = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Object rendering pipelines
	rasterization_state.cullMode = vk::CullModeFlagBits::eBack;

	// Vertex bindings an attributes for model rendering
	// Binding description
	std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex)),
	};

	// Attribute descriptions
	std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, 0),                        // Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32B32Sfloat, sizeof(float) * 3),        // Normal
	    vkb::initializers::vertex_input_attribute_description(0, 2, vk::Format::eR32G32Sfloat, sizeof(float) * 6),           // UV
	};

	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	pipeline_create_info.pVertexInputState = &vertex_input_state;

	// Skybox pipeline (background cube)
	blend_attachment_state.blendEnable = VK_FALSE;
	pipeline_create_info.layout        = pipeline_layouts.models;
	pipeline_create_info.renderPass    = offscreen.render_pass;
	color_blend_state.attachmentCount  = 2;
	color_blend_state.pAttachments     = blend_attachment_states.data();

	shader_stages[0] = load_shader("hdr/gbuffer.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("hdr/gbuffer.frag", vk::ShaderStageFlagBits::eFragment);

	// Set constant parameters via specialization constants
	specialization_map_entries[0]        = vkb::initializers::specialization_map_entry(0, 0, sizeof(uint32_t));
	uint32_t shadertype                  = 0;
	specialization_info                  = vkb::initializers::specialization_info(1, specialization_map_entries.data(), sizeof(shadertype), &shadertype);
	shader_stages[0].pSpecializationInfo = &specialization_info;
	shader_stages[1].pSpecializationInfo = &specialization_info;

	pipelines.skybox = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Object rendering pipeline
	shadertype = 1;

	// Enable depth test and write
	depth_stencil_state.depthWriteEnable = VK_TRUE;
	depth_stencil_state.depthTestEnable  = VK_TRUE;
	// Flip cull mode
	rasterization_state.cullMode = vk::CullModeFlagBits::eFront;
	pipelines.reflect            = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

// Prepare and initialize uniform buffer containing shader uniforms
void HDR::prepare_uniform_buffers()
{
	// Matrices vertex shader uniform buffer
	uniform_buffers.matrices = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                               sizeof(ubo_vs),
	                                                               vk::BufferUsageFlagBits::eUniformBuffer,
	                                                               vma::MemoryUsage::eCpuToGpu);

	// Params
	uniform_buffers.params = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                             sizeof(ubo_params),
	                                                             vk::BufferUsageFlagBits::eUniformBuffer,
	                                                             vma::MemoryUsage::eCpuToGpu);

	update_uniform_buffers();
	update_params();
}

void HDR::update_uniform_buffers()
{
	ubo_vs.projection       = camera.matrices.perspective;
	ubo_vs.modelview        = camera.matrices.view * models.transforms[models.object_index];
	ubo_vs.skybox_modelview = camera.matrices.view;
	uniform_buffers.matrices->convert_and_update(ubo_vs);
}

void HDR::update_params()
{
	uniform_buffers.params->convert_and_update(ubo_params);
}

void HDR::draw()
{
	ApiVulkanSample::prepare_frame();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];
	queue.submit(submit_info, nullptr);
	ApiVulkanSample::submit_frame();
}

bool HDR::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	camera.type = vkb::CameraType::LookAt;
	camera.set_position(glm::vec3(0.0f, 0.0f, -4.0f));
	camera.set_rotation(glm::vec3(0.0f, 180.0f, 0.0f));

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.set_perspective(60.0f, (float) width / (float) height, 256.0f, 0.1f);

	load_assets();
	prepare_uniform_buffers();
	prepare_offscreen_buffer();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_sets();
	build_command_buffers();
	prepared = true;
	return true;
}

void HDR::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
	if (camera.updated)
		update_uniform_buffers();
}

void HDR::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Settings"))
	{
		if (drawer.combo_box("Object type", &models.object_index, object_names))
		{
			update_uniform_buffers();
			build_command_buffers();
		}
		if (drawer.input_float("Exposure", &ubo_params.exposure, 0.025f, 3))
		{
			update_params();
		}
		if (drawer.checkbox("Bloom", &bloom))
		{
			build_command_buffers();
		}
		if (drawer.checkbox("Skybox", &display_skybox))
		{
			build_command_buffers();
		}
	}
}

void HDR::resize(const uint32_t width, const uint32_t height)
{
	ApiVulkanSample::resize(width, height);
	update_uniform_buffers();
}

std::unique_ptr<vkb::Application> create_hdr()
{
	return std::make_unique<HDR>();
}
