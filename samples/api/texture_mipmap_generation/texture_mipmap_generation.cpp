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
 * Runtime mip map generation
 */

#include "texture_mipmap_generation.h"

TextureMipMapGeneration::TextureMipMapGeneration()
{
	zoom     = -2.5f;
	rotation = {0.0f, 15.0f, 0.0f};
	title    = "Texture MipMap generation";
}

TextureMipMapGeneration::~TextureMipMapGeneration()
{
	if (device)
	{
		get_device().get_handle().destroy(pipeline);
		get_device().get_handle().destroy(pipeline_layout);
		get_device().get_handle().destroy(descriptor_set_layout);
		for (auto sampler : samplers)
		{
			get_device().get_handle().destroy(sampler);
		}
	}
	destroy_texture(texture);
	uniform_buffer.reset();
}

// Enable physical device features required for this example
void TextureMipMapGeneration::get_device_features()
{
	// Enable anisotropic filtering if supported
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	};
}

/*
	Load the base texture containing only the first mip level and generate the whole mip-chain at runtime
*/
void TextureMipMapGeneration::load_texture_generate_mipmaps(std::string file_name)
{
	vk::Format format = vk::Format::eR8G8B8A8Unorm;

	ktxTexture *   ktx_texture;
	KTX_error_code result;

	result = ktxTexture_CreateFromNamedFile(file_name.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
	// @todo: get format from libktx

	if (ktx_texture == nullptr)
	{
		throw std::runtime_error("Couldn't load texture");
	}
	texture.width  = ktx_texture->baseWidth;
	texture.height = ktx_texture->baseHeight;
	// Calculate number of mip levels as per Vulkan specs:
	// numLevels = 1 + floor(log2(max(w, h, d)))
	texture.mip_levels = static_cast<uint32_t>(floor(log2(std::max(texture.width, texture.height))) + 1);

	// Get device properites for the requested texture format
	// Check if the selected format supports blit source and destination, which is required for generating the mip levels
	// If this is not supported you could implement a fallback via compute shader image writes and stores
	vk::FormatProperties formatProperties = get_device().get_physical_device().getFormatProperties(format);
	if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc) || !(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst))
	{
		throw std::runtime_error("Selected image format does not support blit source and destination");
	}

	vk::MemoryAllocateInfo memory_allocate_info = vkb::initializers::memory_allocate_info();
	vk::MemoryRequirements memory_requirements  = {};

	ktx_uint8_t *ktx_image_data   = ktxTexture_GetData(ktx_texture);
	ktx_size_t   ktx_texture_size = ktxTexture_GetSize(ktx_texture);

	// Create a host-visible staging buffer that contains the raw image data
	vk::Buffer       staging_buffer;
	vk::DeviceMemory staging_memory;

	// This buffer is used as a transfer source for the buffer copy
	vk::BufferCreateInfo buffer_create_info = vkb::initializers::buffer_create_info();

	buffer_create_info.size  = ktx_texture_size;
	buffer_create_info.usage = vk::BufferUsageFlagBits::eTransferSrc;

	staging_buffer = get_device().get_handle().createBuffer(buffer_create_info);

	// Get memory requirements for the staging buffer (alignment, memory type bits)
	memory_requirements                 = get_device().get_handle().getBufferMemoryRequirements(staging_buffer);
	memory_allocate_info.allocationSize = memory_requirements.size;
	// Get memory type index for a host visible buffer
	memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	staging_memory                       = get_device().get_handle().allocateMemory(memory_allocate_info);
	get_device().get_handle().bindBufferMemory(staging_buffer, staging_memory, 0);

	// Copy ktx image data into host local staging buffer
	void *data = get_device().get_handle().mapMemory(staging_memory, 0, memory_requirements.size, {});
	memcpy(data, ktx_image_data, ktx_texture_size);
	get_device().get_handle().unmapMemory(staging_memory);

	// Create optimal tiled target image on the device
	vk::ImageCreateInfo image_create_info = vkb::initializers::image_create_info();
	image_create_info.imageType           = vk::ImageType::e2D;
	image_create_info.format              = format;
	image_create_info.mipLevels           = texture.mip_levels;
	image_create_info.arrayLayers         = 1;
	image_create_info.samples             = vk::SampleCountFlagBits::e1;
	image_create_info.tiling              = vk::ImageTiling::eOptimal;
	image_create_info.sharingMode         = vk::SharingMode::eExclusive;
	image_create_info.initialLayout       = vk::ImageLayout::eUndefined;
	image_create_info.extent              = {texture.width, texture.height, 1};
	image_create_info.usage               = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
	texture.image                         = get_device().get_handle().createImage(image_create_info);

	memory_requirements                  = get_device().get_handle().getImageMemoryRequirements(texture.image);
	memory_allocate_info.allocationSize  = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	texture.device_memory                = get_device().get_handle().allocateMemory(memory_allocate_info);
	get_device().get_handle().bindImageMemory(texture.image, texture.device_memory, 0);

	vk::CommandBuffer copy_command = device->create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

	// Optimal image will be used as destination for the copy, so we must transfer from our initial undefined image layout to the transfer destination layout
	vkb::insert_image_memory_barrier(
	    copy_command,
	    texture.image,
	    {},
	    vk::AccessFlagBits::eTransferWrite,
	    vk::ImageLayout::eUndefined,
	    vk::ImageLayout::eTransferDstOptimal,
	    vk::PipelineStageFlagBits::eTransfer,
	    vk::PipelineStageFlagBits::eTransfer,
	    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

	// Copy the first mip of the chain, remaining mips will be generated
	vk::BufferImageCopy buffer_copy_region             = {};
	buffer_copy_region.imageSubresource.aspectMask     = vk::ImageAspectFlagBits::eColor;
	buffer_copy_region.imageSubresource.mipLevel       = 0;
	buffer_copy_region.imageSubresource.baseArrayLayer = 0;
	buffer_copy_region.imageSubresource.layerCount     = 1;
	buffer_copy_region.imageExtent.width               = texture.width;
	buffer_copy_region.imageExtent.height              = texture.height;
	buffer_copy_region.imageExtent.depth               = 1;
	copy_command.copyBufferToImage(staging_buffer, texture.image, vk::ImageLayout::eTransferDstOptimal, buffer_copy_region);

	// Transition first mip level to transfer source so we can blit(read) from it
	vkb::insert_image_memory_barrier(
	    copy_command,
	    texture.image,
	    vk::AccessFlagBits::eTransferWrite,
	    vk::AccessFlagBits::eTransferRead,
	    vk::ImageLayout::eTransferDstOptimal,
	    vk::ImageLayout::eTransferSrcOptimal,
	    vk::PipelineStageFlagBits::eTransfer,
	    vk::PipelineStageFlagBits::eTransfer,
	    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});

	device->flush_command_buffer(copy_command, queue, true);

	// Clean up staging resources
	device->get_handle().freeMemory(staging_memory);
	device->get_handle().destroy(staging_buffer);

	// Generate the mip chain
	// ---------------------------------------------------------------
	// We copy down the whole mip chain doing a blit from mip-1 to mip
	// An alternative way would be to always blit from the first mip level and sample that one down
	vk::CommandBuffer blit_command = device->create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

	// Copy down mips from n-1 to n
	for (uint32_t i = 1; i < texture.mip_levels; i++)
	{
		vk::ImageBlit image_blit;

		// Source
		image_blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		image_blit.srcSubresource.layerCount = 1;
		image_blit.srcSubresource.mipLevel   = i - 1;
		image_blit.srcOffsets[1].x           = int32_t(texture.width >> (i - 1));
		image_blit.srcOffsets[1].y           = int32_t(texture.height >> (i - 1));
		image_blit.srcOffsets[1].z           = 1;

		// Destination
		image_blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		image_blit.dstSubresource.layerCount = 1;
		image_blit.dstSubresource.mipLevel   = i;
		image_blit.dstOffsets[1].x           = int32_t(texture.width >> i);
		image_blit.dstOffsets[1].y           = int32_t(texture.height >> i);
		image_blit.dstOffsets[1].z           = 1;

		// Prepare current mip level as image blit destination
		vkb::insert_image_memory_barrier(
		    blit_command,
		    texture.image,
		    {},
		    vk::AccessFlagBits::eTransferWrite,
		    vk::ImageLayout::eUndefined,
		    vk::ImageLayout::eTransferDstOptimal,
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eTransfer,
		    {vk::ImageAspectFlagBits::eColor, i, 1, 0, 1});

		// Blit from previous level
		blit_command.blitImage(
		    texture.image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    texture.image,
		    vk::ImageLayout::eTransferDstOptimal,
		    image_blit,
		    vk::Filter::eLinear);

		// Prepare current mip level as image blit source for next level
		vkb::insert_image_memory_barrier(
		    blit_command,
		    texture.image,
		    vk::AccessFlagBits::eTransferWrite,
		    vk::AccessFlagBits::eTransferRead,
		    vk::ImageLayout::eTransferDstOptimal,
		    vk::ImageLayout::eTransferSrcOptimal,
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eTransfer,
		    {vk::ImageAspectFlagBits::eColor, i, 1, 0, 1});
	}

	// After the loop, all mip layers are in TRANSFER_SRC layout, so transition all to SHADER_READ
	vkb::insert_image_memory_barrier(
	    blit_command,
	    texture.image,
	    vk::AccessFlagBits::eTransferRead,
	    vk::AccessFlagBits::eShaderRead,
	    vk::ImageLayout::eTransferSrcOptimal,
	    vk::ImageLayout::eShaderReadOnlyOptimal,
	    vk::PipelineStageFlagBits::eTransfer,
	    vk::PipelineStageFlagBits::eFragmentShader,
	    {vk::ImageAspectFlagBits::eColor, 0, texture.mip_levels, 0, 1});

	device->flush_command_buffer(blit_command, queue, true);
	// ---------------------------------------------------------------

	// Create samplers for different mip map demonstration cases
	samplers.resize(3);
	vk::SamplerCreateInfo sampler = vkb::initializers::sampler_create_info();
	sampler.magFilter             = vk::Filter::eLinear;
	sampler.minFilter             = vk::Filter::eLinear;
	sampler.mipmapMode            = vk::SamplerMipmapMode::eLinear;
	sampler.addressModeU          = vk::SamplerAddressMode::eRepeat;
	sampler.addressModeV          = vk::SamplerAddressMode::eRepeat;
	sampler.addressModeW          = vk::SamplerAddressMode::eRepeat;
	sampler.mipLodBias            = 0.0f;
	sampler.compareOp             = vk::CompareOp::eNever;
	sampler.minLod                = 0.0f;
	sampler.maxLod                = 0.0f;
	sampler.borderColor           = vk::BorderColor::eFloatOpaqueWhite;
	sampler.maxAnisotropy         = 1.0;
	sampler.anisotropyEnable      = VK_FALSE;

	// Without mip mapping
	samplers[0] = device->get_handle().createSampler(sampler);

	// With mip mapping
	sampler.maxLod = static_cast<float>(texture.mip_levels);
	samplers[1]    = device->get_handle().createSampler(sampler);

	// With mip mapping and anisotropic filtering (when supported)
	if (device->get_features().samplerAnisotropy)
	{
		sampler.maxAnisotropy    = device->get_properties().limits.maxSamplerAnisotropy;
		sampler.anisotropyEnable = VK_TRUE;
	}
	samplers[2] = device->get_handle().createSampler(sampler);

	// Create image view
	vk::ImageViewCreateInfo view = vkb::initializers::image_view_create_info();

	view.image                           = texture.image;
	view.viewType                        = vk::ImageViewType::e2D;
	view.format                          = format;
	view.components                      = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
	view.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
	view.subresourceRange.baseMipLevel   = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount     = 1;
	view.subresourceRange.levelCount     = texture.mip_levels;
	texture.view                         = device->get_handle().createImageView(view);
}

// Free all Vulkan resources used by a texture object
void TextureMipMapGeneration::destroy_texture(Texture texture)
{
	get_device().get_handle().destroy(texture.view);
	get_device().get_handle().destroy(texture.image);
	get_device().get_handle().freeMemory(texture.device_memory);
}

void TextureMipMapGeneration::load_assets()
{
	load_texture_generate_mipmaps(vkb::fs::path::get(vkb::fs::path::Assets, "textures/checkerboard_rgba.ktx"));
	scene = load_model("scenes/tunnel_cylinder.gltf");
}

void TextureMipMapGeneration::build_command_buffers()
{
	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	vk::ClearValue clear_values[2];
	clear_values[0].color        = default_clear_color;
	clear_values[1].depthStencil = {1.0f, 0};

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

		draw_cmd_buffers[i].begin(command_buffer_begin_info);
		draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
		draw_cmd_buffers[i].setViewport(0, viewport);

		vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
		draw_cmd_buffers[i].setScissor(0, scissor);

		draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptor_set, nullptr);
		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

		draw_model(scene, draw_cmd_buffers[i]);

		draw_ui(draw_cmd_buffers[i]);

		draw_cmd_buffers[i].endRenderPass();

		draw_cmd_buffers[i].end();
	}
}

void TextureMipMapGeneration::draw()
{
	ApiVulkanSample::prepare_frame();

	// Command buffer to be sumitted to the queue
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];

	// Submit to queue
	queue.submit(submit_info, nullptr);

	ApiVulkanSample::submit_frame();
}

void TextureMipMapGeneration::setup_descriptor_pool()
{
	// Example uses one ubo and one image sampler
	std::vector<vk::DescriptorPoolSize> pool_sizes =
	    {
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 1),
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eSampledImage, 1),
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eSampler, 3),
	    };

	vk::DescriptorPoolCreateInfo descriptor_pool_create_info =
	    vkb::initializers::descriptor_pool_create_info(pool_sizes, 2);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);
}

void TextureMipMapGeneration::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings =
	    {
	        // Binding 0 : Parameter uniform buffer
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eUniformBuffer,
	            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
	            0),
	        // Binding 1 : Fragment shader image sampler
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eSampledImage,
	            vk::ShaderStageFlagBits::eFragment,
	            1),
	        // Binding 2 : Sampler array (3 descriptors)
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eSampler,
	            vk::ShaderStageFlagBits::eFragment,
	            2,
	            3),
	    };

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

void TextureMipMapGeneration::setup_descriptor_set()
{
	vk::DescriptorSetAllocateInfo alloc_info =
	    vkb::initializers::descriptor_set_allocate_info(
	        descriptor_pool,
	        &descriptor_set_layout,
	        1);

	descriptor_set = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo buffer_descriptor = create_descriptor(*uniform_buffer);

	vk::DescriptorImageInfo image_descriptor;
	image_descriptor.imageView   = texture.view;
	image_descriptor.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	std::vector<vk::WriteDescriptorSet> write_descriptor_sets =
	    {
	        // Binding 0 : Vertex shader uniform buffer
	        vkb::initializers::write_descriptor_set(
	            descriptor_set,
	            vk::DescriptorType::eUniformBuffer,
	            0,
	            &buffer_descriptor),
	        // Binding 1 : Fragment shader texture sampler
	        vkb::initializers::write_descriptor_set(
	            descriptor_set,
	            vk::DescriptorType::eSampledImage,
	            1,
	            &image_descriptor)};

	// Binding 2: Sampler array
	std::vector<vk::DescriptorImageInfo> sampler_descriptors;
	for (auto i = 0; i < samplers.size(); i++)
	{
		sampler_descriptors.push_back(vk::DescriptorImageInfo{samplers[i], nullptr, vk::ImageLayout::eShaderReadOnlyOptimal});
	}
	vk::WriteDescriptorSet write_descriptor_set;
	write_descriptor_set.dstSet          = descriptor_set;
	write_descriptor_set.descriptorType  = vk::DescriptorType::eSampler;
	write_descriptor_set.descriptorCount = static_cast<uint32_t>(sampler_descriptors.size());
	write_descriptor_set.pImageInfo      = sampler_descriptors.data();
	write_descriptor_set.dstBinding      = 2;
	write_descriptor_set.dstArrayElement = 0;
	write_descriptor_sets.push_back(write_descriptor_set);
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void TextureMipMapGeneration::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info();

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(
	        vk::CullModeFlagBits::eNone);

	vk::PipelineColorBlendAttachmentState blend_attachment_state =
	    vkb::initializers::pipeline_color_blend_attachment_state();

	vk::PipelineColorBlendStateCreateInfo color_blend_state =
	    vkb::initializers::pipeline_color_blend_state_create_info(
	        1,
	        &blend_attachment_state);

	vk::PipelineDepthStencilStateCreateInfo depth_stencil_state =
	    vkb::initializers::pipeline_depth_stencil_state_create_info(
	        VK_TRUE,
	        VK_TRUE,
	        vk::CompareOp::eLessOrEqual);

	vk::PipelineViewportStateCreateInfo viewport_state =
	    vkb::initializers::pipeline_viewport_state_create_info(1, 1);

	vk::PipelineMultisampleStateCreateInfo multisample_state =
	    vkb::initializers::pipeline_multisample_state_create_info();

	std::vector<vk::DynamicState> dynamic_state_enables = {
	    vk::DynamicState::eViewport,
	    vk::DynamicState::eScissor};

	vk::PipelineDynamicStateCreateInfo dynamic_state =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	// Load shaders
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages;

	shader_stages[0] = load_shader("texture_mipmap_generation/texture.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("texture_mipmap_generation/texture.frag", vk::ShaderStageFlagBits::eFragment);

	// Vertex bindings and attributes
	const std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(Vertex)),
	};
	const std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, 0),                        // Location 0: Position
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32Sfloat, sizeof(float) * 6),           // Location 1: UV
	    vkb::initializers::vertex_input_attribute_description(0, 2, vk::Format::eR32G32B32Sfloat, sizeof(float) * 8),        // Location 2: Color
	};
	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	vk::GraphicsPipelineCreateInfo pipeline_create_info = vkb::initializers::pipeline_create_info(pipeline_layout, render_pass);
	pipeline_create_info.pVertexInputState              = &vertex_input_state;
	pipeline_create_info.pInputAssemblyState            = &input_assembly_state;
	pipeline_create_info.pRasterizationState            = &rasterization_state;
	pipeline_create_info.pColorBlendState               = &color_blend_state;
	pipeline_create_info.pMultisampleState              = &multisample_state;
	pipeline_create_info.pViewportState                 = &viewport_state;
	pipeline_create_info.pDepthStencilState             = &depth_stencil_state;
	pipeline_create_info.pDynamicState                  = &dynamic_state;
	pipeline_create_info.stageCount                     = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages                        = shader_stages.data();

	pipeline = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

void TextureMipMapGeneration::prepare_uniform_buffers()
{
	// Shared parameter uniform buffer block
	uniform_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                     sizeof(ubo),
	                                                     vk::BufferUsageFlagBits::eUniformBuffer,
	                                                     vma::MemoryUsage::eCpuToGpu);

	update_uniform_buffers();
}

void TextureMipMapGeneration::update_uniform_buffers(float delta_time)
{
	ubo.projection = camera.matrices.perspective;
	ubo.model      = camera.matrices.view;
	ubo.model      = glm::rotate(ubo.model, glm::radians(90.0f + timer * 360.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	ubo.model      = glm::scale(ubo.model, glm::vec3(0.5f));
	timer += delta_time * 0.005f;
	if (timer > 1.0f)
	{
		timer -= 1.0f;
	}
	uniform_buffer->convert_and_update(ubo);
}

bool TextureMipMapGeneration::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	camera.type = vkb::CameraType::FirstPerson;
	camera.set_perspective(60.0f, (float) width / (float) height, 0.1f, 1024.0f);
	camera.set_translation(glm::vec3(0.0f, 0.0f, -12.5f));

	load_assets();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_set();
	build_command_buffers();

	prepared = true;
	return true;
}

void TextureMipMapGeneration::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
	if (rotate_scene)
		update_uniform_buffers(delta_time);
}

void TextureMipMapGeneration::view_changed()
{
	update_uniform_buffers();
}

void TextureMipMapGeneration::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Settings"))
	{
		drawer.checkbox("Rotate", &rotate_scene);
		if (drawer.slider_float("LOD bias", &ubo.lod_bias, 0.0f, (float) texture.mip_levels))
		{
			update_uniform_buffers();
		}
		if (drawer.combo_box("Sampler type", &ubo.sampler_index, sampler_names))
		{
			update_uniform_buffers();
		}
	}
}

std::unique_ptr<vkb::Application> create_texture_mipmap_generation()
{
	return std::make_unique<TextureMipMapGeneration>();
}
