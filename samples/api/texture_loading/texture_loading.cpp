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
 * Texture loading (and display) example (including mip maps)
 */

#include "texture_loading.h"

TextureLoading::TextureLoading()
{
	zoom     = -2.5f;
	rotation = {0.0f, 15.0f, 0.0f};
	title    = "Texture loading";
}

TextureLoading::~TextureLoading()
{
	if (device)
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class

		get_device().get_handle().destroy(pipelines.solid);

		get_device().get_handle().destroy(pipeline_layout);
		get_device().get_handle().destroy(descriptor_set_layout);
	}

	destroy_texture(texture);

	vertex_buffer.reset();
	index_buffer.reset();
	uniform_buffer_vs.reset();
}

// Enable physical device features required for this example
void TextureLoading::get_device_features()
{
	// Enable anisotropic filtering if supported
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	};
}

/*
	Upload texture image data to the GPU

	Vulkan offers two types of image tiling (memory layout):

	Linear tiled images:
		These are stored as is and can be copied directly to. But due to the linear nature they're not a good match for GPUs and format and feature support is very limited.
		It's not advised to use linear tiled images for anything else than copying from host to GPU if buffer copies are not an option.
		Linear tiling is thus only implemented for learning purposes, one should always prefer optimal tiled image.

	Optimal tiled images:
		These are stored in an implementation specific layout matching the capability of the hardware. They usually support more formats and features and are much faster.
		Optimal tiled images are stored on the device and not accessible by the host. So they can't be written directly to (like liner tiled images) and always require 
		some sort of data copy, either from a buffer or	a linear tiled image.
		
	In Short: Always use optimal tiled images for rendering.
*/
void TextureLoading::load_texture()
{
	// We use the Khronos texture format (https://www.khronos.org/opengles/sdk/tools/KTX/file_format_spec/)
	std::string filename = vkb::fs::path::get(vkb::fs::path::Assets, "textures/metalplate01_rgba.ktx");
	// Texture data contains 4 channels (RGBA) with unnormalized 8-bit values, this is the most commonly supported format
	vk::Format format = vk::Format::eR8G8B8A8Unorm;

	ktxTexture *   ktx_texture;
	KTX_error_code result;

	result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);

	if (ktx_texture == nullptr)
	{
		throw std::runtime_error("Couldn't load texture");
	}

	//assert(!tex2D.empty());

	texture.width      = ktx_texture->baseWidth;
	texture.height     = ktx_texture->baseHeight;
	texture.mip_levels = ktx_texture->numLevels;

	// We prefer using staging to copy the texture data to a device local optimal image
	vk::Bool32 use_staging = true;

	// Only use linear tiling if forced
	bool force_linear_tiling = false;
	if (force_linear_tiling)
	{
		// Don't use linear if format is not supported for (linear) shader sampling
		// Get device properites for the requested texture format
		vk::FormatProperties format_properties = get_device().get_physical_device().getFormatProperties(format);
		use_staging                            = !(format_properties.linearTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);
	}

	vk::MemoryAllocateInfo memory_allocate_info = vkb::initializers::memory_allocate_info();
	vk::MemoryRequirements memory_requirements  = {};

	ktx_uint8_t *ktx_image_data   = ktxTexture_GetData(ktx_texture);
	ktx_size_t   ktx_texture_size = ktxTexture_GetSize(ktx_texture);

	if (use_staging)
	{
		// Copy data to an optimal tiled image
		// This loads the texture data into a host local buffer that is copied to the optimal tiled image on the device

		// Create a host-visible staging buffer that contains the raw image data
		// This buffer will be the data source for copying texture data to the optimal tiled image on the device
		vk::Buffer       staging_buffer;
		vk::DeviceMemory staging_memory;

		vk::BufferCreateInfo buffer_create_info = vkb::initializers::buffer_create_info();
		buffer_create_info.size                 = ktx_texture_size;
		// This buffer is used as a transfer source for the buffer copy
		buffer_create_info.usage = vk::BufferUsageFlagBits::eTransferSrc;
		staging_buffer           = get_device().get_handle().createBuffer(buffer_create_info);

		// Get memory requirements for the staging buffer (alignment, memory type bits)
		memory_requirements                 = get_device().get_handle().getBufferMemoryRequirements(staging_buffer);
		memory_allocate_info.allocationSize = memory_requirements.size;
		// Get memory type index for a host visible buffer
		memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		staging_memory                       = get_device().get_handle().allocateMemory(memory_allocate_info);
		get_device().get_handle().bindBufferMemory(staging_buffer, staging_memory, 0);

		// Copy texture data into host local staging buffer

		void *data = get_device().get_handle().mapMemory(staging_memory, 0, memory_requirements.size, {});
		memcpy(data, ktx_image_data, ktx_texture_size);
		get_device().get_handle().unmapMemory(staging_memory);

		// Setup buffer copy regions for each mip level
		std::vector<vk::BufferImageCopy> buffer_copy_regions;
		for (uint32_t i = 0; i < texture.mip_levels; i++)
		{
			ktx_size_t          offset;
			KTX_error_code      result                         = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
			vk::BufferImageCopy buffer_copy_region             = {};
			buffer_copy_region.imageSubresource.aspectMask     = vk::ImageAspectFlagBits::eColor;
			buffer_copy_region.imageSubresource.mipLevel       = i;
			buffer_copy_region.imageSubresource.baseArrayLayer = 0;
			buffer_copy_region.imageSubresource.layerCount     = 1;
			buffer_copy_region.imageExtent.width               = ktx_texture->baseWidth >> i;
			buffer_copy_region.imageExtent.height              = ktx_texture->baseHeight >> i;
			buffer_copy_region.imageExtent.depth               = 1;
			buffer_copy_region.bufferOffset                    = offset;
			buffer_copy_regions.push_back(buffer_copy_region);
		}

		// Create optimal tiled target image on the device
		vk::ImageCreateInfo image_create_info = vkb::initializers::image_create_info();
		image_create_info.imageType           = vk::ImageType::e2D;
		image_create_info.format              = format;
		image_create_info.mipLevels           = texture.mip_levels;
		image_create_info.arrayLayers         = 1;
		image_create_info.samples             = vk::SampleCountFlagBits::e1;
		image_create_info.tiling              = vk::ImageTiling::eOptimal;
		// Set initial layout of the image to undefined
		image_create_info.initialLayout = vk::ImageLayout::eUndefined;
		image_create_info.extent        = {texture.width, texture.height, 1};
		image_create_info.usage         = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
		texture.image                   = get_device().get_handle().createImage(image_create_info);

		memory_requirements                  = get_device().get_handle().getImageMemoryRequirements(texture.image);
		memory_allocate_info.allocationSize  = memory_requirements.size;
		memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
		texture.device_memory                = get_device().get_handle().allocateMemory(memory_allocate_info);
		get_device().get_handle().bindImageMemory(texture.image, texture.device_memory, 0);

		vk::CommandBuffer copy_command = device->create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

		// Image memory barriers for the texture image

		// The sub resource range describes the regions of the image that will be transitioned using the memory barriers below
		vk::ImageSubresourceRange subresource_range = {};
		// Image only contains color data
		subresource_range.aspectMask = vk::ImageAspectFlagBits::eColor;
		// Start at first mip level
		subresource_range.baseMipLevel = 0;
		// We will transition on all mip levels
		subresource_range.levelCount = texture.mip_levels;
		// The 2D texture only has one layer
		subresource_range.layerCount = 1;

		// Transition the texture image layout to transfer target, so we can safely copy our buffer data to it.
		vk::ImageMemoryBarrier image_memory_barrier = vkb::initializers::image_memory_barrier();

		image_memory_barrier.image            = texture.image;
		image_memory_barrier.subresourceRange = subresource_range;
		image_memory_barrier.srcAccessMask    = {};
		image_memory_barrier.dstAccessMask    = vk::AccessFlagBits::eTransferWrite;
		image_memory_barrier.oldLayout        = vk::ImageLayout::eUndefined;
		image_memory_barrier.newLayout        = vk::ImageLayout::eTransferDstOptimal;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage is host write/read exection (vk::PipelineStageFlagBits::eHOST)
		// Destination pipeline stage is copy command exection (vk::PipelineStageFlagBits::eTransfer)
		copy_command.pipelineBarrier(
		    vk::PipelineStageFlagBits::eHost,
		    vk::PipelineStageFlagBits::eTransfer,
		    {},
		    nullptr,
		    nullptr,
		    image_memory_barrier);

		// Copy mip levels from staging buffer
		copy_command.copyBufferToImage(
		    staging_buffer,
		    texture.image,
		    vk::ImageLayout::eTransferDstOptimal,
		    buffer_copy_regions);

		// Once the data has been uploaded we transfer to the texture image to the shader read layout, so it can be sampled from
		image_memory_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		image_memory_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		image_memory_barrier.oldLayout     = vk::ImageLayout::eTransferDstOptimal;
		image_memory_barrier.newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage stage is copy command exection (vk::PipelineStageFlagBits::eTransfer)
		// Destination pipeline stage fragment shader access (vk::PipelineStageFlagBits::eFragmentShader)
		copy_command.pipelineBarrier(
		    vk::PipelineStageFlagBits::eTransfer,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    {},
		    nullptr,
		    nullptr,
		    image_memory_barrier);

		// Store current layout for later reuse
		texture.image_layout = vk::ImageLayout::eShaderReadOnlyOptimal;

		device->flush_command_buffer(copy_command, queue, true);

		// Clean up staging resources
		get_device().get_handle().freeMemory(staging_memory);
		get_device().get_handle().destroy(staging_buffer);
	}
	else
	{
		// Copy data to a linear tiled image

		vk::Image        mappable_image;
		vk::DeviceMemory mappable_memory;

		// Load mip map level 0 to linear tiling image
		vk::ImageCreateInfo image_create_info = vkb::initializers::image_create_info();
		image_create_info.imageType           = vk::ImageType::e2D;
		image_create_info.format              = format;
		image_create_info.mipLevels           = 1;
		image_create_info.arrayLayers         = 1;
		image_create_info.samples             = vk::SampleCountFlagBits::e1;
		image_create_info.tiling              = vk::ImageTiling::eLinear;
		image_create_info.usage               = vk::ImageUsageFlagBits::eSampled;
		image_create_info.initialLayout       = vk::ImageLayout::ePreinitialized;
		image_create_info.extent              = {texture.width, texture.height, 1};
		mappable_image                        = get_device().get_handle().createImage(image_create_info);

		// Get memory requirements for this image like size and alignment
		memory_requirements = get_device().get_handle().getImageMemoryRequirements(mappable_image);
		// Set memory allocation size to required memory size
		memory_allocate_info.allocationSize = memory_requirements.size;
		// Get memory type that can be mapped to host memory
		memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
		mappable_memory                      = get_device().get_handle().allocateMemory(memory_allocate_info);
		get_device().get_handle().bindImageMemory(mappable_image, mappable_memory, 0);

		// Map image memory
		ktx_size_t ktx_image_size = ktxTexture_GetImageSize(ktx_texture, 0);

		void *data = get_device().get_handle().mapMemory(mappable_memory, 0, memory_requirements.size, {});
		// Copy image data of the first mip level into memory
		memcpy(data, ktx_image_data, ktx_image_size);
		get_device().get_handle().unmapMemory(mappable_memory);

		// Linear tiled images don't need to be staged and can be directly used as textures
		texture.image         = mappable_image;
		texture.device_memory = mappable_memory;
		texture.image_layout  = vk::ImageLayout::eShaderReadOnlyOptimal;

		// Setup image memory barrier transfer image to shader read layout
		vk::CommandBuffer copy_command = device->create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

		// The sub resource range describes the regions of the image we will be transition
		vk::ImageSubresourceRange subresource_range;
		subresource_range.aspectMask   = vk::ImageAspectFlagBits::eColor;
		subresource_range.baseMipLevel = 0;
		subresource_range.levelCount   = 1;
		subresource_range.layerCount   = 1;

		// Transition the texture image layout to shader read, so it can be sampled from
		vk::ImageMemoryBarrier image_memory_barrier = vkb::initializers::image_memory_barrier();

		image_memory_barrier.image            = texture.image;
		image_memory_barrier.subresourceRange = subresource_range;
		image_memory_barrier.srcAccessMask    = vk::AccessFlagBits::eHostWrite;
		image_memory_barrier.dstAccessMask    = vk::AccessFlagBits::eShaderRead;
		image_memory_barrier.oldLayout        = vk::ImageLayout::ePreinitialized;
		image_memory_barrier.newLayout        = vk::ImageLayout::eShaderReadOnlyOptimal;

		// Insert a memory dependency at the proper pipeline stages that will execute the image layout transition
		// Source pipeline stage is host write/read exection (vk::PipelineStageFlagBits::eHOST)
		// Destination pipeline stage fragment shader access (vk::PipelineStageFlagBits::eFragmentShader)
		copy_command.pipelineBarrier(
		    vk::PipelineStageFlagBits::eHost,
		    vk::PipelineStageFlagBits::eFragmentShader,
		    {},
		    nullptr,
		    nullptr,
		    image_memory_barrier);

		device->flush_command_buffer(copy_command, queue, true);
	}

	// Create a texture sampler
	// In Vulkan textures are accessed by samplers
	// This separates all the sampling information from the texture data. This means you could have multiple sampler objects for the same texture with different settings
	// Note: Similar to the samplers available with OpenGL 3.3
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
	// Set max level-of-detail to mip level count of the texture
	sampler.maxLod = (use_staging) ? (float) texture.mip_levels : 0.0f;
	// Enable anisotropic filtering
	// This feature is optional, so we must check if it's supported on the device
	if (get_device().get_features().samplerAnisotropy)
	{
		// Use max. level of anisotropy for this example
		sampler.maxAnisotropy    = get_device().get_properties().limits.maxSamplerAnisotropy;
		sampler.anisotropyEnable = VK_TRUE;
	}
	else
	{
		// The device does not support anisotropic filtering
		sampler.maxAnisotropy    = 1.0;
		sampler.anisotropyEnable = VK_FALSE;
	}
	sampler.borderColor = vk::BorderColor::eFloatOpaqueWhite;
	texture.sampler     = get_device().get_handle().createSampler(sampler);

	// Create image view
	// Textures are not directly accessed by the shaders and
	// are abstracted by image views containing additional
	// information and sub resource ranges
	vk::ImageViewCreateInfo view = vkb::initializers::image_view_create_info();
	view.viewType                = vk::ImageViewType::e2D;
	view.format                  = format;
	view.components              = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
	// The subresource range describes the set of mip levels (and array layers) that can be accessed through this image view
	// It's possible to create multiple image views for a single image referring to different (and/or overlapping) ranges of the image
	view.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
	view.subresourceRange.baseMipLevel   = 0;
	view.subresourceRange.baseArrayLayer = 0;
	view.subresourceRange.layerCount     = 1;
	// Linear tiling usually won't support mip maps
	// Only set mip map count if optimal tiling is used
	view.subresourceRange.levelCount = (use_staging) ? texture.mip_levels : 1;
	// The view will be based on the texture's image
	view.image   = texture.image;
	texture.view = get_device().get_handle().createImageView(view);
}

// Free all Vulkan resources used by a texture object
void TextureLoading::destroy_texture(Texture texture)
{
	get_device().get_handle().destroy(texture.view);
	get_device().get_handle().destroy(texture.image);
	get_device().get_handle().destroy(texture.sampler);
	get_device().get_handle().freeMemory(texture.device_memory, nullptr);
}

void TextureLoading::build_command_buffers()
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
		// Set target frame buffer
		render_pass_begin_info.framebuffer = framebuffers[i];

		draw_cmd_buffers[i].begin(command_buffer_begin_info);

		draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
		draw_cmd_buffers[i].setViewport(0, viewport);

		vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
		draw_cmd_buffers[i].setScissor(0, scissor);

		draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptor_set, nullptr);
		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.solid);

		auto vertexBuffer = vertex_buffer->get_handle();
		draw_cmd_buffers[i].bindVertexBuffers(0, vertexBuffer, {0});
		auto indexBuffer = index_buffer->get_handle();
		draw_cmd_buffers[i].bindIndexBuffer(indexBuffer, 0, vk::IndexType::eUint32);

		draw_cmd_buffers[i].drawIndexed(index_count, 1, 0, 0, 0);

		draw_ui(draw_cmd_buffers[i]);

		draw_cmd_buffers[i].endRenderPass();

		draw_cmd_buffers[i].end();
	}
}

void TextureLoading::draw()
{
	ApiVulkanSample::prepare_frame();

	// Command buffer to be sumitted to the queue
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];

	// Submit to queue
	queue.submit(submit_info, nullptr);

	ApiVulkanSample::submit_frame();
}

void TextureLoading::generate_quad()
{
	// Setup vertices for a single uv-mapped quad made from two triangles
	std::vector<TextureLoadingVertexStructure> vertices{
	    {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{1.0f, -1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	};

	// Setup indices
	std::vector<uint32_t> indices{0, 1, 2, 2, 3, 0};
	index_count = static_cast<uint32_t>(indices.size());

	auto vertex_buffer_size = vkb::to_u32(vertices.size() * sizeof(TextureLoadingVertexStructure));
	auto index_buffer_size  = vkb::to_u32(indices.size() * sizeof(uint32_t));

	// Create buffers
	// For the sake of simplicity we won't stage the vertex data to the gpu memory
	// Vertex buffer
	vertex_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                    vertex_buffer_size,
	                                                    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
	                                                    vma::MemoryUsage::eCpuToGpu);
	vertex_buffer->update(vertices.data(), vertex_buffer_size);

	index_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                   index_buffer_size,
	                                                   vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
	                                                   vma::MemoryUsage::eCpuToGpu);

	index_buffer->update(indices.data(), index_buffer_size);
}

void TextureLoading::setup_descriptor_pool()
{
	// Example uses one ubo and one image sampler
	std::vector<vk::DescriptorPoolSize> pool_sizes =
	    {
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 1),
	        vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 1)};

	vk::DescriptorPoolCreateInfo descriptor_pool_create_info =
	    vkb::initializers::descriptor_pool_create_info(
	        pool_sizes,
	        2);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);
}

void TextureLoading::setup_descriptor_set_layout()
{
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings =
	    {
	        // Binding 0 : Vertex shader uniform buffer
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eUniformBuffer,
	            vk::ShaderStageFlagBits::eVertex,
	            0),
	        // Binding 1 : Fragment shader image sampler
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eCombinedImageSampler,
	            vk::ShaderStageFlagBits::eFragment,
	            1)};

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

void TextureLoading::setup_descriptor_set()
{
	vk::DescriptorSetAllocateInfo alloc_info =
	    vkb::initializers::descriptor_set_allocate_info(
	        descriptor_pool,
	        &descriptor_set_layout,
	        1);

	descriptor_set = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo buffer_descriptor = create_descriptor(*uniform_buffer_vs);

	// Setup a descriptor image info for the current texture to be used as a combined image sampler
	vk::DescriptorImageInfo image_descriptor;
	image_descriptor.imageView   = texture.view;                // The image's view (images are never directly accessed by the shader, but rather through views defining subresources)
	image_descriptor.sampler     = texture.sampler;             // The sampler (Telling the pipeline how to sample the texture, including repeat, border, etc.)
	image_descriptor.imageLayout = texture.image_layout;        // The current layout of the image (Note: Should always fit the actual use, e.g. shader read)

	std::vector<vk::WriteDescriptorSet> write_descriptor_sets =
	    {
	        // Binding 0 : Vertex shader uniform buffer
	        vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eUniformBuffer, 0, &buffer_descriptor),
	        // Binding 1 : Fragment shader texture sampler
	        //	Fragment shader: layout (binding = 1) uniform sampler2D samplerColor;
	        // The descriptor set will use a combined image sampler (sampler and image could be split)
	        vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eCombinedImageSampler, 1, &image_descriptor)};

	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void TextureLoading::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state =
	    vkb::initializers::pipeline_input_assembly_state_create_info();

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(
	        vk::PolygonMode::eFill,
	        vk::CullModeFlagBits::eNone,
	        vk::FrontFace::eCounterClockwise);

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
	    vk::DynamicState::eScissor};

	vk::PipelineDynamicStateCreateInfo dynamic_state =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	// Load shaders
	std::array<vk::PipelineShaderStageCreateInfo, 2> shader_stages;

	shader_stages[0] = load_shader("texture_loading/texture.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("texture_loading/texture.frag", vk::ShaderStageFlagBits::eFragment);

	// Vertex bindings and attributes
	const std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(TextureLoadingVertexStructure)),
	};
	const std::vector<vk::VertexInputAttributeDescription> vertex_input_attributes = {
	    vkb::initializers::vertex_input_attribute_description(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(TextureLoadingVertexStructure, pos)),
	    vkb::initializers::vertex_input_attribute_description(0, 1, vk::Format::eR32G32Sfloat, offsetof(TextureLoadingVertexStructure, uv)),
	    vkb::initializers::vertex_input_attribute_description(0, 2, vk::Format::eR32G32B32Sfloat, offsetof(TextureLoadingVertexStructure, normal)),
	};
	vk::PipelineVertexInputStateCreateInfo vertex_input_state = vkb::initializers::pipeline_vertex_input_state_create_info();
	vertex_input_state.vertexBindingDescriptionCount          = static_cast<uint32_t>(vertex_input_bindings.size());
	vertex_input_state.pVertexBindingDescriptions             = vertex_input_bindings.data();
	vertex_input_state.vertexAttributeDescriptionCount        = static_cast<uint32_t>(vertex_input_attributes.size());
	vertex_input_state.pVertexAttributeDescriptions           = vertex_input_attributes.data();

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(
	        pipeline_layout,
	        render_pass);

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

	pipelines.solid = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

// Prepare and initialize uniform buffer containing shader uniforms
void TextureLoading::prepare_uniform_buffers()
{
	// Vertex shader uniform buffer block
	uniform_buffer_vs = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                        sizeof(ubo_vs),
	                                                        vk::BufferUsageFlagBits::eUniformBuffer,
	                                                        vma::MemoryUsage::eCpuToGpu);

	update_uniform_buffers();
}

void TextureLoading::update_uniform_buffers()
{
	// Vertex shader
	ubo_vs.projection     = glm::perspective(glm::radians(60.0f), (float) width / (float) height, 0.001f, 256.0f);
	glm::mat4 view_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

	ubo_vs.model = view_matrix * glm::translate(glm::mat4(1.0f), camera_pos);
	ubo_vs.model = glm::rotate(ubo_vs.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	ubo_vs.model = glm::rotate(ubo_vs.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	ubo_vs.model = glm::rotate(ubo_vs.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

	ubo_vs.view_pos = glm::vec4(0.0f, 0.0f, -zoom, 0.0f);

	uniform_buffer_vs->convert_and_update(ubo_vs);
}

bool TextureLoading::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}
	load_texture();
	generate_quad();
	prepare_uniform_buffers();
	setup_descriptor_set_layout();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_set();
	build_command_buffers();
	prepared = true;
	return true;
}

void TextureLoading::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
}

void TextureLoading::view_changed()
{
	update_uniform_buffers();
}

void TextureLoading::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Settings"))
	{
		if (drawer.slider_float("LOD bias", &ubo_vs.lod_bias, 0.0f, (float) texture.mip_levels))
		{
			update_uniform_buffers();
		}
	}
}

std::unique_ptr<vkb::Application> create_texture_loading()
{
	return std::make_unique<TextureLoading>();
}
