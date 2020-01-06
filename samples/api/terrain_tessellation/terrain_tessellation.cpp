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
 * Dynamic terrain tessellation
 */

#include "terrain_tessellation.h"

#include "heightmap.h"

TerrainTessellation::TerrainTessellation()
{
	title = "Dynamic terrain tessellation";
}

TerrainTessellation::~TerrainTessellation()
{
	if (device)
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class
		get_device().get_handle().destroy(pipelines.terrain);
		if (pipelines.wireframe)
		{
			get_device().get_handle().destroy(pipelines.wireframe);
		}
		get_device().get_handle().destroy(pipelines.skysphere);

		get_device().get_handle().destroy(pipeline_layouts.skysphere);
		get_device().get_handle().destroy(pipeline_layouts.terrain);

		get_device().get_handle().destroy(descriptor_set_layouts.terrain);
		get_device().get_handle().destroy(descriptor_set_layouts.skysphere);

		uniform_buffers.skysphere_vertex.reset();
		uniform_buffers.terrain_tessellation.reset();

		textures.heightmap.image.reset();
		get_device().get_handle().destroy(textures.heightmap.sampler);
		textures.skysphere.image.reset();
		get_device().get_handle().destroy(textures.skysphere.sampler);
		textures.terrain_array.image.reset();
		get_device().get_handle().destroy(textures.terrain_array.sampler);

		if (query_pool)
		{
			get_device().get_handle().destroy(query_pool);
			get_device().get_handle().destroy(query_result.buffer);
			get_device().get_handle().freeMemory(query_result.memory);
		}
	}
}

void TerrainTessellation::get_device_features()
{
	// Tessellation shader support is required for this example
	if (supported_device_features.tessellationShader)
	{
		requested_device_features.tessellationShader = VK_TRUE;
	}
	else
	{
		vk::throwResultException(vk::Result::eErrorFeatureNotPresent, "Selected GPU does not support tessellation shaders!");
	}

	// Fill mode non solid is required for wireframe display
	if (supported_device_features.fillModeNonSolid)
	{
		requested_device_features.fillModeNonSolid = VK_TRUE;
	}

	// Pipeline statistics
	if (supported_device_features.pipelineStatisticsQuery)
	{
		requested_device_features.pipelineStatisticsQuery = VK_TRUE;
	}

	// Enable anisotropic filtering if supported
	if (supported_device_features.samplerAnisotropy)
	{
		requested_device_features.samplerAnisotropy = VK_TRUE;
	}
}

// Setup pool and buffer for storing pipeline statistics results
void TerrainTessellation::setup_query_result_buffer()
{
	uint32_t buffer_size = 2 * sizeof(uint64_t);

	vk::MemoryRequirements memory_requirements;
	vk::MemoryAllocateInfo memory_allocation = vkb::initializers::memory_allocate_info();
	vk::BufferCreateInfo   buffer_create_info =
	    vkb::initializers::buffer_create_info(
	        vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
	        buffer_size);

	// Results are saved in a host visible buffer for easy access by the application
	query_result.buffer               = get_device().get_handle().createBuffer(buffer_create_info);
	memory_requirements               = get_device().get_handle().getBufferMemoryRequirements(query_result.buffer);
	memory_allocation.allocationSize  = memory_requirements.size;
	memory_allocation.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	query_result.memory               = get_device().get_handle().allocateMemory(memory_allocation);
	get_device().get_handle().bindBufferMemory(query_result.buffer, query_result.memory, 0);

	// Create query pool
	if (get_device().get_features().pipelineStatisticsQuery)
	{
		vk::QueryPoolCreateInfo query_pool_info = {};
		query_pool_info.queryType               = vk::QueryType::ePipelineStatistics;
		query_pool_info.pipelineStatistics =
		    vk::QueryPipelineStatisticFlagBits::eVertexShaderInvocations |
		    vk::QueryPipelineStatisticFlagBits::eTessellationEvaluationShaderInvocations;
		query_pool_info.queryCount = 2;
		query_pool                 = get_device().get_handle().createQueryPool(query_pool_info);
	}
}

// Retrieves the results of the pipeline statistics query submitted to the command buffer
void TerrainTessellation::get_query_results()
{
	// We use vkGetQueryResults to copy the results into a host visible buffer
	get_device().get_handle().getQueryPoolResults(
	    query_pool,
	    0,
	    1,
	    sizeof(pipeline_stats),
	    pipeline_stats,
	    sizeof(uint64_t),
	    vk::QueryResultFlagBits::e64);
}

void TerrainTessellation::load_assets()
{
	// @todo: sascha
	skysphere = load_model("scenes/geosphere.gltf");

	textures.skysphere = load_texture("textures/skysphere_rgba.ktx");
	// Terrain textures are stored in a texture array with layers corresponding to terrain height
	textures.terrain_array = load_texture_array("textures/terrain_texturearray_rgba.ktx");

	// Height data is stored in a one-channel texture
	textures.heightmap = load_texture("textures/terrain_heightmap_r16.ktx");

	vk::SamplerCreateInfo sampler_create_info = vkb::initializers::sampler_create_info();

	// Setup a mirroring sampler for the height map
	get_device().get_handle().destroy(textures.heightmap.sampler);
	sampler_create_info.magFilter    = vk::Filter::eLinear;
	sampler_create_info.minFilter    = vk::Filter::eLinear;
	sampler_create_info.mipmapMode   = vk::SamplerMipmapMode::eLinear;
	sampler_create_info.addressModeU = vk::SamplerAddressMode::eMirroredRepeat;
	sampler_create_info.addressModeV = sampler_create_info.addressModeU;
	sampler_create_info.addressModeW = sampler_create_info.addressModeU;
	sampler_create_info.compareOp    = vk::CompareOp::eNever;
	sampler_create_info.minLod       = 0.0f;
	sampler_create_info.maxLod       = static_cast<float>(textures.heightmap.image->get_mipmaps().size());
	sampler_create_info.borderColor  = vk::BorderColor::eFloatOpaqueWhite;
	textures.heightmap.sampler       = get_device().get_handle().createSampler(sampler_create_info);

	// Setup a repeating sampler for the terrain texture layers
	vk::Sampler sampler;
	get_device().get_handle().destroy(textures.terrain_array.sampler);
	sampler_create_info              = vkb::initializers::sampler_create_info();
	sampler_create_info.magFilter    = vk::Filter::eLinear;
	sampler_create_info.minFilter    = vk::Filter::eLinear;
	sampler_create_info.mipmapMode   = vk::SamplerMipmapMode::eLinear;
	sampler_create_info.addressModeU = vk::SamplerAddressMode::eRepeat;
	sampler_create_info.addressModeV = sampler_create_info.addressModeU;
	sampler_create_info.addressModeW = sampler_create_info.addressModeU;
	sampler_create_info.compareOp    = vk::CompareOp::eNever;
	sampler_create_info.minLod       = 0.0f;
	sampler_create_info.maxLod       = static_cast<float>(textures.terrain_array.image->get_mipmaps().size());
	sampler_create_info.borderColor  = vk::BorderColor::eFloatOpaqueWhite;
	if (get_device().get_features().samplerAnisotropy)
	{
		sampler_create_info.maxAnisotropy    = 4.0f;
		sampler_create_info.anisotropyEnable = VK_TRUE;
	}
	sampler = get_device().get_handle().createSampler(sampler_create_info);

	vkb::core::Sampler vk_sampler{get_device(), sampler_create_info};

	textures.terrain_array.sampler = sampler;
}

void TerrainTessellation::build_command_buffers()
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

		draw_cmd_buffers[i].begin(command_buffer_begin_info);

		if (get_device().get_features().pipelineStatisticsQuery)
		{
			draw_cmd_buffers[i].resetQueryPool(query_pool, 0, 2);
		}

		draw_cmd_buffers[i].beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

		vk::Viewport viewport = vkb::initializers::viewport((float) width, (float) height, 0.0f, 1.0f);
		draw_cmd_buffers[i].setViewport(0, viewport);

		vk::Rect2D scissor = vkb::initializers::rect2D(width, height, 0, 0);
		draw_cmd_buffers[i].setScissor(0, scissor);

		draw_cmd_buffers[i].setLineWidth(1.0f);

		// Skysphere
		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, pipelines.skysphere);
		draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.skysphere, 0, descriptor_sets.skysphere, nullptr);
		draw_model(skysphere, draw_cmd_buffers[i]);

		// Terrain
		if (get_device().get_features().pipelineStatisticsQuery)
		{
			// Begin pipeline statistics query
			draw_cmd_buffers[i].beginQuery(query_pool, 0, {});
		}
		// Render
		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eGraphics, wireframe ? pipelines.wireframe : pipelines.terrain);
		draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layouts.terrain, 0, descriptor_sets.terrain, nullptr);
		draw_cmd_buffers[i].bindVertexBuffers(0, terrain.vertices->get_handle(), {0});
		draw_cmd_buffers[i].bindIndexBuffer(terrain.indices->get_handle(), 0, vk::IndexType::eUint32);
		draw_cmd_buffers[i].drawIndexed(terrain.index_count, 1, 0, 0, 0);
		if (get_device().get_features().pipelineStatisticsQuery)
		{
			// End pipeline statistics query
			draw_cmd_buffers[i].endQuery(query_pool, 0);
		}

		draw_ui(draw_cmd_buffers[i]);

		draw_cmd_buffers[i].endRenderPass();

		draw_cmd_buffers[i].end();
	}
}

// Generate a terrain quad patch for feeding to the tessellation control shader
void TerrainTessellation::generate_terrain()
{
#define PATCH_SIZE 64
#define UV_SCALE 1.0f

	constexpr uint32_t vertex_count = PATCH_SIZE * PATCH_SIZE;
    std::array<Vertex, vertex_count> vertices;

	const float wx = 2.0f;
	const float wy = 2.0f;

	for (auto x = 0; x < PATCH_SIZE; x++)
	{
		for (auto y = 0; y < PATCH_SIZE; y++)
		{
			uint32_t index         = (x + y * PATCH_SIZE);
			vertices[index].pos[0] = x * wx + wx / 2.0f - (float) PATCH_SIZE * wx / 2.0f;
			vertices[index].pos[1] = 0.0f;
			vertices[index].pos[2] = y * wy + wy / 2.0f - (float) PATCH_SIZE * wy / 2.0f;
			vertices[index].uv     = glm::vec2((float) x / PATCH_SIZE, (float) y / PATCH_SIZE) * UV_SCALE;
		}
	}

	// Calculate normals from height map using a sobel filter
	vkb::HeightMap heightmap("textures/terrain_heightmap_r16.ktx", PATCH_SIZE);
	for (auto x = 0; x < PATCH_SIZE; x++)
	{
		for (auto y = 0; y < PATCH_SIZE; y++)
		{
			// Get height samples centered around current position
			float heights[3][3];
			for (auto hx = -1; hx <= 1; hx++)
			{
				for (auto hy = -1; hy <= 1; hy++)
				{
					heights[hx + 1][hy + 1] = heightmap.get_height(x + hx, y + hy);
				}
			}

			// Calculate the normal
			glm::vec3 normal;
			// Gx sobel filter
			normal.x = heights[0][0] - heights[2][0] + 2.0f * heights[0][1] - 2.0f * heights[2][1] + heights[0][2] - heights[2][2];
			// Gy sobel filter
			normal.z = heights[0][0] + 2.0f * heights[1][0] + heights[2][0] - heights[0][2] - 2.0f * heights[1][2] - heights[2][2];
			// Calculate missing up component of the normal using the filtered x and y axis
			// The first value controls the bump strength
			normal.y = 0.25f * sqrt(1.0f - normal.x * normal.x - normal.z * normal.z);

			vertices[x + y * PATCH_SIZE].normal = glm::normalize(normal * glm::vec3(2.0f, 1.0f, 2.0f));
		}
	}

	// Indices
	constexpr uint32_t w           = (PATCH_SIZE - 1);
	constexpr uint32_t index_count = w * w * 4;
    std::array<uint32_t, index_count> indices;
    for (auto x = 0; x < w; x++)
	{
		for (auto y = 0; y < w; y++)
		{
			uint32_t index     = (x + y * w) * 4;
			indices[index]     = (x + y * PATCH_SIZE);
			indices[index + 1] = indices[index] + PATCH_SIZE;
			indices[index + 2] = indices[index + 1] + 1;
			indices[index + 3] = indices[index] + 1;
		}
	}
	terrain.index_count = index_count;

	terrain.vertices = std::make_unique<vkb::core::Buffer>(
        get_device().stage_to_device_buffer(
            vertices,
            vk::BufferUsageFlagBits::eVertexBuffer));

    terrain.indices = std::make_unique<vkb::core::Buffer>(
        get_device().stage_to_device_buffer(
            indices,
            vk::BufferUsageFlagBits::eIndexBuffer));
}

void TerrainTessellation::setup_descriptor_pool()
{
	std::vector<vk::DescriptorPoolSize> pool_sizes{
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eUniformBuffer, 3),
	    vkb::initializers::descriptor_pool_size(vk::DescriptorType::eCombinedImageSampler, 3),
	};

	vk::DescriptorPoolCreateInfo descriptor_pool_create_info =
	    vkb::initializers::descriptor_pool_create_info(pool_sizes, 2);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);
}

void TerrainTessellation::setup_descriptor_set_layouts()
{
	vk::DescriptorSetLayoutCreateInfo           descriptor_layout;
	vk::PipelineLayoutCreateInfo                pipeline_layout_create_info;
	std::vector<vk::DescriptorSetLayoutBinding> set_layout_bindings;

	// Terrain
	set_layout_bindings =
	    {
	        // Binding 0 : Shared Tessellation shader ubo
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eUniformBuffer,
	            vk::ShaderStageFlagBits::eTessellationControl | vk::ShaderStageFlagBits::eTessellationEvaluation,
	            0),
	        // Binding 1 : Height map
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eCombinedImageSampler,
	            vk::ShaderStageFlagBits::eTessellationControl | vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
	            1),
	        // Binding 3 : Terrain texture array layers
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eCombinedImageSampler,
	            vk::ShaderStageFlagBits::eFragment,
	            2),
	    };

	descriptor_layout              = vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	descriptor_set_layouts.terrain = get_device().get_handle().createDescriptorSetLayout(descriptor_layout);
	pipeline_layout_create_info    = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layouts.terrain, 1);
	pipeline_layouts.terrain       = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);

	// Skysphere
	set_layout_bindings =
	    {
	        // Binding 0 : Vertex shader ubo
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eUniformBuffer,
	            vk::ShaderStageFlagBits::eVertex,
	            0),
	        // Binding 1 : Color map
	        vkb::initializers::descriptor_set_layout_binding(
	            vk::DescriptorType::eCombinedImageSampler,
	            vk::ShaderStageFlagBits::eFragment,
	            1),
	    };

	descriptor_layout                = vkb::initializers::descriptor_set_layout_create_info(set_layout_bindings.data(), static_cast<uint32_t>(set_layout_bindings.size()));
	descriptor_set_layouts.skysphere = get_device().get_handle().createDescriptorSetLayout(descriptor_layout);
	pipeline_layout_create_info      = vkb::initializers::pipeline_layout_create_info(&descriptor_set_layouts.skysphere, 1);
	pipeline_layouts.skysphere       = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);
}

void TerrainTessellation::setup_descriptor_sets()
{
	vk::DescriptorSetAllocateInfo       alloc_info;
	std::vector<vk::WriteDescriptorSet> write_descriptor_sets;

	// Terrain
	alloc_info              = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layouts.terrain, 1);
	descriptor_sets.terrain = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo terrain_buffer_descriptor   = create_descriptor(*uniform_buffers.terrain_tessellation);
	vk::DescriptorImageInfo  heightmap_image_descriptor  = create_descriptor(textures.heightmap);
	vk::DescriptorImageInfo  terrainmap_image_descriptor = create_descriptor(textures.terrain_array);
	write_descriptor_sets =
	    {
	        // Binding 0 : Shared tessellation shader ubo
	        vkb::initializers::write_descriptor_set(
	            descriptor_sets.terrain,
	            vk::DescriptorType::eUniformBuffer,
	            0,
	            &terrain_buffer_descriptor),
	        // Binding 1 : Displacement map
	        vkb::initializers::write_descriptor_set(
	            descriptor_sets.terrain,
	            vk::DescriptorType::eCombinedImageSampler,
	            1,
	            &heightmap_image_descriptor),
	        // Binding 2 : Color map (alpha channel)
	        vkb::initializers::write_descriptor_set(
	            descriptor_sets.terrain,
	            vk::DescriptorType::eCombinedImageSampler,
	            2,
	            &terrainmap_image_descriptor),
	    };
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);

	// Skysphere
	alloc_info                = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layouts.skysphere, 1);
	descriptor_sets.skysphere = get_device().get_handle().allocateDescriptorSets(alloc_info)[0];

	vk::DescriptorBufferInfo skysphere_buffer_descriptor = create_descriptor(*uniform_buffers.skysphere_vertex);
	vk::DescriptorImageInfo  skysphere_image_descriptor  = create_descriptor(textures.skysphere);
	write_descriptor_sets =
	    {
	        // Binding 0 : Vertex shader ubo
	        vkb::initializers::write_descriptor_set(
	            descriptor_sets.skysphere,
	            vk::DescriptorType::eUniformBuffer,
	            0,
	            &skysphere_buffer_descriptor),
	        // Binding 1 : Fragment shader color map
	        vkb::initializers::write_descriptor_set(
	            descriptor_sets.skysphere,
	            vk::DescriptorType::eCombinedImageSampler,
	            1,
	            &skysphere_image_descriptor),
	    };
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

void TerrainTessellation::prepare_pipelines()
{
	vk::PipelineInputAssemblyStateCreateInfo input_assembly_state_create_info =
	    vkb::initializers::pipeline_input_assembly_state_create_info(
	        vk::PrimitiveTopology::ePatchList);

	vk::PipelineRasterizationStateCreateInfo rasterization_state =
	    vkb::initializers::pipeline_rasterization_state_create_info(
	        vk::PolygonMode::eFill,
	        vk::CullModeFlagBits::eBack,
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
	    vk::DynamicState::eScissor,
	    vk::DynamicState::eLineWidth};

	vk::PipelineDynamicStateCreateInfo dynamic_state =
	    vkb::initializers::pipeline_dynamic_state_create_info(dynamic_state_enables);

	// We render the terrain as a grid of quad patches
	vk::PipelineTessellationStateCreateInfo tessellation_state =
	    vkb::initializers::pipeline_tessellation_state_create_info(4);

	// Vertex bindings an attributes
	// Binding description
	std::vector<vk::VertexInputBindingDescription> vertex_input_bindings = {
	    vkb::initializers::vertex_input_binding_description(0, sizeof(TerrainTessellation::Vertex)),
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

	std::array<vk::PipelineShaderStageCreateInfo, 4> shader_stages;

	// Terrain tessellation pipeline
	shader_stages[0] = load_shader("terrain_tessellation/terrain.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1] = load_shader("terrain_tessellation/terrain.frag", vk::ShaderStageFlagBits::eFragment);
	shader_stages[2] = load_shader("terrain_tessellation/terrain.tesc", vk::ShaderStageFlagBits::eTessellationControl);
	shader_stages[3] = load_shader("terrain_tessellation/terrain.tese", vk::ShaderStageFlagBits::eTessellationEvaluation);

	vk::GraphicsPipelineCreateInfo pipeline_create_info =
	    vkb::initializers::pipeline_create_info(pipeline_layouts.terrain, render_pass);

	pipeline_create_info.pVertexInputState   = &vertex_input_state;
	pipeline_create_info.pInputAssemblyState = &input_assembly_state_create_info;
	pipeline_create_info.pRasterizationState = &rasterization_state;
	pipeline_create_info.pColorBlendState    = &color_blend_state;
	pipeline_create_info.pMultisampleState   = &multisample_state;
	pipeline_create_info.pViewportState      = &viewport_state;
	pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
	pipeline_create_info.pDynamicState       = &dynamic_state;
	pipeline_create_info.pTessellationState  = &tessellation_state;
	pipeline_create_info.stageCount          = static_cast<uint32_t>(shader_stages.size());
	pipeline_create_info.pStages             = shader_stages.data();
	pipeline_create_info.renderPass          = render_pass;

	pipelines.terrain = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);

	// Terrain wireframe pipeline
	if (get_device().get_features().fillModeNonSolid)
	{
		rasterization_state.polygonMode = vk::PolygonMode::eLine;
		pipelines.wireframe             = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
	};

	// Skysphere pipeline

	// Stride from glTF model vertex layout
	vertex_input_bindings[0].stride = sizeof(::Vertex);

	rasterization_state.polygonMode = vk::PolygonMode::eFill;
	// Revert to triangle list topology
	input_assembly_state_create_info.topology = vk::PrimitiveTopology::eTriangleList;
	// Reset tessellation state
	pipeline_create_info.pTessellationState = nullptr;
	// Don't write to depth buffer
	depth_stencil_state.depthWriteEnable = VK_FALSE;
	pipeline_create_info.stageCount      = 2;
	pipeline_create_info.layout          = pipeline_layouts.skysphere;
	shader_stages[0]                     = load_shader("terrain_tessellation/skysphere.vert", vk::ShaderStageFlagBits::eVertex);
	shader_stages[1]                     = load_shader("terrain_tessellation/skysphere.frag", vk::ShaderStageFlagBits::eFragment);
	pipelines.skysphere                  = get_device().get_handle().createGraphicsPipeline(pipeline_cache, pipeline_create_info);
}

// Prepare and initialize uniform buffer containing shader uniforms
void TerrainTessellation::prepare_uniform_buffers()
{
	// Shared tessellation shader stages uniform buffer
	uniform_buffers.terrain_tessellation = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                                           sizeof(ubo_tess),
	                                                                           vk::BufferUsageFlagBits::eUniformBuffer,
	                                                                           vma::MemoryUsage::eCpuToGpu);

	// Skysphere vertex shader uniform buffer
	uniform_buffers.skysphere_vertex = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                                       sizeof(ubo_vs),
	                                                                       vk::BufferUsageFlagBits::eUniformBuffer,
	                                                                       vma::MemoryUsage::eCpuToGpu);

	update_uniform_buffers();
}

void TerrainTessellation::update_uniform_buffers()
{
	// Tessellation

	ubo_tess.projection   = camera.matrices.perspective;
	ubo_tess.modelview    = camera.matrices.view * glm::mat4(1.0f);
	ubo_tess.light_pos.y  = -0.5f - ubo_tess.displacement_factor;        // todo: Not uesed yet
	ubo_tess.viewport_dim = glm::vec2((float) width, (float) height);

	frustum.update(ubo_tess.projection * ubo_tess.modelview);
	memcpy(ubo_tess.frustum_planes, frustum.get_planes().data(), sizeof(glm::vec4) * 6);

	float saved_factor = ubo_tess.tessellation_factor;
	if (!tessellation)
	{
		// Setting this to zero sets all tessellation factors to 1.0 in the shader
		ubo_tess.tessellation_factor = 0.0f;
	}

	uniform_buffers.terrain_tessellation->convert_and_update(ubo_tess);

	if (!tessellation)
	{
		ubo_tess.tessellation_factor = saved_factor;
	}

	// Skysphere vertex shader
	ubo_vs.mvp = camera.matrices.perspective * glm::mat4(glm::mat3(camera.matrices.view));
	uniform_buffers.skysphere_vertex->convert_and_update(ubo_vs.mvp);
}

void TerrainTessellation::draw()
{
	ApiVulkanSample::prepare_frame();

	// Command buffer to be sumitted to the queue
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];

	// Submit to queue
	queue.submit(submit_info, nullptr);

	if (get_device().get_features().pipelineStatisticsQuery)
	{
		// Read query results for displaying in next frame
		get_query_results();
	}

	ApiVulkanSample::submit_frame();
}

bool TerrainTessellation::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.type = vkb::CameraType::FirstPerson;
	camera.set_perspective(60.0f, (float) width / (float) height, 512.0f, 0.1f);
	camera.set_rotation(glm::vec3(-12.0f, 159.0f, 0.0f));
	camera.set_translation(glm::vec3(18.0f, 22.5f, 57.5f));
	camera.translation_speed = 7.5f;

	load_assets();
	generate_terrain();
	if (get_device().get_features().pipelineStatisticsQuery)
	{
		setup_query_result_buffer();
	}
	prepare_uniform_buffers();
	setup_descriptor_set_layouts();
	prepare_pipelines();
	setup_descriptor_pool();
	setup_descriptor_sets();
	build_command_buffers();
	prepared = true;
	return true;
}

void TerrainTessellation::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
}

void TerrainTessellation::view_changed()
{
	update_uniform_buffers();
}

void TerrainTessellation::on_update_ui_overlay(vkb::Drawer &drawer)
{
	if (drawer.header("Settings"))
	{
		if (drawer.checkbox("Tessellation", &tessellation))
		{
			update_uniform_buffers();
		}
		if (drawer.input_float("Factor", &ubo_tess.tessellation_factor, 0.05f, 2))
		{
			update_uniform_buffers();
		}
		if (get_device().get_features().fillModeNonSolid)
		{
			if (drawer.checkbox("Wireframe", &wireframe))
			{
				build_command_buffers();
			}
		}
	}
	if (get_device().get_features().pipelineStatisticsQuery)
	{
		if (drawer.header("Pipeline statistics"))
		{
			drawer.text("VS invocations: %d", pipeline_stats[0]);
			drawer.text("TE invocations: %d", pipeline_stats[1]);
		}
	}
}

std::unique_ptr<vkb::Application> create_terrain_tessellation()
{
	return std::make_unique<TerrainTessellation>();
}
