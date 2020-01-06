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
 * Basic example for ray tracing using VK_NV_ray_tracing
 */

#include "raytracing_basic.h"

RaytracingBasic::RaytracingBasic()
{
	title = "VK_NV_ray_tracing";

	// Enable instance and device extensions required to use VK_NV_ray_tracing
	instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	device_extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	device_extensions.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
}

RaytracingBasic::~RaytracingBasic()
{
	if (device)
	{
		get_device().get_handle().destroy(pipeline);
		get_device().get_handle().destroy(pipeline_layout);
		get_device().get_handle().destroy(descriptor_set_layout);
		get_device().get_handle().destroy(storage_image.view);
		get_device().get_handle().destroy(storage_image.image);
		get_device().get_handle().freeMemory(storage_image.memory);
		get_device().get_handle().freeMemory(bottom_level_acceleration_structure.memory);
		get_device().get_handle().freeMemory(top_level_acceleration_structure.memory);
		get_device().get_handle().destroy(bottom_level_acceleration_structure.acceleration_structure);
		get_device().get_handle().destroy(top_level_acceleration_structure.acceleration_structure);
		vertex_buffer.reset();
		index_buffer.reset();
		shader_binding_table.reset();
		ubo.reset();
	}
}

/*
	Set up a storage image that the ray generation shader will be writing to
*/
void RaytracingBasic::create_storage_image()
{
	vk::ImageCreateInfo image = vkb::initializers::image_create_info();
	image.imageType           = vk::ImageType::e2D;
	image.format              = vk::Format::eB8G8R8A8Unorm;
	image.extent.width        = width;
	image.extent.height       = height;
	image.extent.depth        = 1;
	image.mipLevels           = 1;
	image.arrayLayers         = 1;
	image.samples             = vk::SampleCountFlagBits::e1;
	image.tiling              = vk::ImageTiling::eOptimal;
	image.usage               = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
	image.initialLayout       = vk::ImageLayout::eUndefined;
	storage_image.image       = get_device().get_handle().createImage(image);

	vk::MemoryRequirements memory_requirements = get_device().get_handle().getImageMemoryRequirements(storage_image.image);

	vk::MemoryAllocateInfo memory_allocate_info;
	memory_allocate_info.allocationSize  = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = get_device().get_memory_type(memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);

	storage_image.memory = get_device().get_handle().allocateMemory(memory_allocate_info);
	get_device().get_handle().bindImageMemory(storage_image.image, storage_image.memory, 0);

	vk::ImageViewCreateInfo color_image_view         = vkb::initializers::image_view_create_info();
	color_image_view.viewType                        = vk::ImageViewType::e2D;
	color_image_view.format                          = vk::Format::eB8G8R8A8Unorm;
	color_image_view.subresourceRange                = {};
	color_image_view.subresourceRange.aspectMask     = vk::ImageAspectFlagBits::eColor;
	color_image_view.subresourceRange.baseMipLevel   = 0;
	color_image_view.subresourceRange.levelCount     = 1;
	color_image_view.subresourceRange.baseArrayLayer = 0;
	color_image_view.subresourceRange.layerCount     = 1;
	color_image_view.image                           = storage_image.image;
	storage_image.view                               = get_device().get_handle().createImageView(color_image_view);

	vk::CommandBuffer command_buffer = get_device().create_command_buffer(vk::CommandBufferLevel::ePrimary, true);
	vkb::set_image_layout(command_buffer, storage_image.image,
	                      vk::ImageLayout::eUndefined,
	                      vk::ImageLayout::eGeneral,
	                      {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
	get_device().flush_command_buffer(command_buffer, queue);
}

/*
	The bottom level acceleration structure contains the scene's geometry (vertices, triangles)
*/
void RaytracingBasic::create_bottom_level_acceleration_structure(const vk::GeometryNV *geometries)
{
	vk::AccelerationStructureInfoNV acceleration_structure_info;
	acceleration_structure_info.type          = vk::AccelerationStructureTypeNV::eBottomLevel;
	acceleration_structure_info.instanceCount = 0;
	acceleration_structure_info.geometryCount = 1;
	acceleration_structure_info.pGeometries   = geometries;

	vk::AccelerationStructureCreateInfoNV acceleration_structure_create_info;
	acceleration_structure_create_info.info                    = acceleration_structure_info;
	bottom_level_acceleration_structure.acceleration_structure = get_device().get_handle().createAccelerationStructureNV(acceleration_structure_create_info);

	vk::AccelerationStructureMemoryRequirementsInfoNV memory_requirements;

	memory_requirements.type                  = vk::AccelerationStructureMemoryRequirementsTypeNV::eObject;
	memory_requirements.accelerationStructure = bottom_level_acceleration_structure.acceleration_structure;

	vk::MemoryRequirements2 memory_requirements2 = get_device().get_handle().getAccelerationStructureMemoryRequirementsNV(memory_requirements);

	vk::MemoryAllocateInfo memory_allocate_info = vkb::initializers::memory_allocate_info();
	memory_allocate_info.allocationSize         = memory_requirements2.memoryRequirements.size;
	memory_allocate_info.memoryTypeIndex        = get_device().get_memory_type(memory_requirements2.memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	bottom_level_acceleration_structure.memory  = get_device().get_handle().allocateMemory(memory_allocate_info);

	vk::BindAccelerationStructureMemoryInfoNV acceleration_structure_memory_info;
	acceleration_structure_memory_info.accelerationStructure = bottom_level_acceleration_structure.acceleration_structure;
	acceleration_structure_memory_info.memory                = bottom_level_acceleration_structure.memory;
	get_device().get_handle().bindAccelerationStructureMemoryNV(acceleration_structure_memory_info);

	// Enhanced C++ binding is broken
	get_device().get_handle().getAccelerationStructureHandleNV(bottom_level_acceleration_structure.acceleration_structure, sizeof(uint64_t), (void *) &bottom_level_acceleration_structure.handle);
}

/*
	The top level acceleration structure contains the scene's object instances
*/
void RaytracingBasic::create_top_level_acceleration_structure()
{
	vk::AccelerationStructureInfoNV acceleration_structure_info;
	acceleration_structure_info.type          = vk::AccelerationStructureTypeNV::eTopLevel;
	acceleration_structure_info.instanceCount = 1;
	acceleration_structure_info.geometryCount = 0;

	vk::AccelerationStructureCreateInfoNV acceleration_structure_create_info;
	acceleration_structure_create_info.info                 = acceleration_structure_info;
	top_level_acceleration_structure.acceleration_structure = get_device().get_handle().createAccelerationStructureNV(acceleration_structure_create_info);

	vk::AccelerationStructureMemoryRequirementsInfoNV memory_requirements;
	memory_requirements.type                  = vk::AccelerationStructureMemoryRequirementsTypeNV::eObject;
	memory_requirements.accelerationStructure = top_level_acceleration_structure.acceleration_structure;

	vk::MemoryRequirements2 memory_requirements2 = get_device().get_handle().getAccelerationStructureMemoryRequirementsNV(memory_requirements);

	vk::MemoryAllocateInfo memory_allocate_info;

	memory_allocate_info.allocationSize     = memory_requirements2.memoryRequirements.size;
	memory_allocate_info.memoryTypeIndex    = get_device().get_memory_type(memory_requirements2.memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	top_level_acceleration_structure.memory = get_device().get_handle().allocateMemory(memory_allocate_info);

	vk::BindAccelerationStructureMemoryInfoNV acceleration_structure_memory_info;
	acceleration_structure_memory_info.accelerationStructure = top_level_acceleration_structure.acceleration_structure;
	acceleration_structure_memory_info.memory                = top_level_acceleration_structure.memory;
	get_device().get_handle().bindAccelerationStructureMemoryNV(acceleration_structure_memory_info);

	// C++ binding for this is broken
	get_device().get_handle().getAccelerationStructureHandleNV(top_level_acceleration_structure.acceleration_structure, sizeof(uint64_t), &top_level_acceleration_structure.handle);
}

/*
	Create scene geometry and ray tracing acceleration structures
*/
void RaytracingBasic::create_scene()
{
	// Setup vertices for a single triangle
	struct Vertex
	{
		float pos[4];
	};
	std::vector<Vertex> vertices = {
	    {{1.0f, 1.0f, 0.0f, 1.0f}},
	    {{-1.0f, 1.0f, 0.0f, 1.0f}},
	    {{0.0f, -1.0f, 0.0f, 1.0f}}};

	// Setup indices
	std::vector<uint32_t> indices = {0, 1, 2};
	index_count                   = static_cast<uint32_t>(indices.size());

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

	// Index buffer
	index_buffer = std::make_unique<vkb::core::Buffer>(get_device(),
	                                                   index_buffer_size,
	                                                   vk::BufferUsageFlagBits::eIndexBuffer,
	                                                   vma::MemoryUsage::eGpuToCpu);
	index_buffer->update(indices.data(), index_buffer_size);

	/*
		Create the bottom level acceleration structure containing the actual scene geometry
	*/
	vk::GeometryNV geometry;
	geometry.geometryType                    = vk::GeometryTypeNV::eTriangles;
	geometry.geometry.triangles.vertexData   = vertex_buffer->get_handle();
	geometry.geometry.triangles.vertexCount  = static_cast<uint32_t>(vertices.size());
	geometry.geometry.triangles.vertexStride = sizeof(Vertex);
	geometry.geometry.triangles.vertexFormat = vk::Format::eR32G32B32Sfloat;
	geometry.geometry.triangles.indexData    = index_buffer->get_handle();
	geometry.geometry.triangles.indexCount   = index_count;
	geometry.geometry.triangles.indexType    = vk::IndexType::eUint32;
	geometry.flags                           = vk::GeometryFlagBitsNV::eOpaque;

	create_bottom_level_acceleration_structure(&geometry);

	/*
		Create the top-level acceleration structure that contains geometry instance information
	*/

	glm::mat3x4 transform = {
	    1.0f,
	    0.0f,
	    0.0f,
	    0.0f,
	    0.0f,
	    1.0f,
	    0.0f,
	    0.0f,
	    0.0f,
	    0.0f,
	    1.0f,
	    0.0f,
	};

	GeometryInstance geometry_instance{};
	geometry_instance.transform                     = transform;
	geometry_instance.instance_id                   = 0;
	geometry_instance.mask                          = 0xff;
	geometry_instance.instance_offset               = 0;
	geometry_instance.flags                         = VK_GEOMETRY_INSTANCE_TRIANGLE_CULL_DISABLE_BIT_NV;
	geometry_instance.acceleration_structure_handle = bottom_level_acceleration_structure.handle;

	// Single instance with a 3x4 transform matrix for the ray traced triangle
	vkb::core::Buffer instance_buffer{get_device(), sizeof(GeometryInstance), vk::BufferUsageFlagBits::eRayTracingNV, vma::MemoryUsage::eCpuOnly};

	instance_buffer.convert_and_update(geometry_instance);

	create_top_level_acceleration_structure();

	/*
		Build the acceleration structure
	*/

	// Acceleration structure build requires some scratch space to store temporary information
	vk::AccelerationStructureMemoryRequirementsInfoNV memory_requirements_info;
	memory_requirements_info.type = vk::AccelerationStructureMemoryRequirementsTypeNV::eBuildScratch;

	vk::MemoryRequirements2 memory_requirements_bottom_level;
	memory_requirements_info.accelerationStructure = bottom_level_acceleration_structure.acceleration_structure;

	memory_requirements_bottom_level = get_device().get_handle().getAccelerationStructureMemoryRequirementsNV(memory_requirements_info);

	vk::MemoryRequirements2 memory_requirements_top_level;
	memory_requirements_info.accelerationStructure = top_level_acceleration_structure.acceleration_structure;

	memory_requirements_top_level = get_device().get_handle().getAccelerationStructureMemoryRequirementsNV(memory_requirements_info);

	const vk::DeviceSize scratch_buffer_size = std::max(memory_requirements_bottom_level.memoryRequirements.size, memory_requirements_top_level.memoryRequirements.size);

	vkb::core::Buffer scratch_buffer{get_device(), scratch_buffer_size, vk::BufferUsageFlagBits::eRayTracingNV, vma::MemoryUsage::eGpuOnly};

	vk::CommandBuffer command_buffer = get_device().create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

	/*
		Build bottom level acceleration structure
	*/
	vk::AccelerationStructureInfoNV build_info;
	build_info.type          = vk::AccelerationStructureTypeNV::eBottomLevel;
	build_info.geometryCount = 1;
	build_info.pGeometries   = &geometry;

	command_buffer.buildAccelerationStructureNV(
	    build_info,
	    nullptr, 0,
	    VK_FALSE,
	    bottom_level_acceleration_structure.acceleration_structure, nullptr,
	    scratch_buffer.get_handle(), 0);

	vk::MemoryBarrier memory_barrier = vkb::initializers::memory_barrier();
	memory_barrier.srcAccessMask     = vk::AccessFlagBits::eAccelerationStructureWriteNV | vk::AccessFlagBits::eAccelerationStructureReadNV;
	memory_barrier.dstAccessMask     = vk::AccessFlagBits::eAccelerationStructureWriteNV | vk::AccessFlagBits::eAccelerationStructureReadNV;
	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, {}, memory_barrier, nullptr, nullptr);

	/*
		Build top-level acceleration structure
	*/
	build_info.type          = vk::AccelerationStructureTypeNV::eTopLevel;
	build_info.pGeometries   = 0;
	build_info.geometryCount = 0;
	build_info.instanceCount = 1;

	command_buffer.buildAccelerationStructureNV(
	    build_info,
	    instance_buffer.get_handle(), 0,
	    VK_FALSE,
	    top_level_acceleration_structure.acceleration_structure, nullptr,
	    scratch_buffer.get_handle(), 0);

	command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, vk::PipelineStageFlagBits::eAccelerationStructureBuildNV, {}, memory_barrier, nullptr, nullptr);

	get_device().flush_command_buffer(command_buffer, queue);
}

vk::DeviceSize RaytracingBasic::copy_shader_identifier(uint8_t *data, const uint8_t *shaderHandleStorage, uint32_t groupIndex)
{
	const uint32_t shader_group_handle_size = ray_tracing_properties.shaderGroupHandleSize;
	memcpy(data, shaderHandleStorage + groupIndex * shader_group_handle_size, shader_group_handle_size);
	data += shader_group_handle_size;
	return shader_group_handle_size;
}

/*
	Create the Shader Binding Table that binds the programs and top-level acceleration structure
*/
void RaytracingBasic::create_shader_binding_table()
{
	// Create buffer for the shader binding table
	const uint32_t shader_binding_table_size = ray_tracing_properties.shaderGroupHandleSize * 3;
	shader_binding_table                     = std::make_unique<vkb::core::Buffer>(get_device(),
                                                               shader_binding_table_size,
                                                               vk::BufferUsageFlagBits::eRayTracingNV,
                                                               vma::MemoryUsage::eGpuToCpu, vma::Allocation::CreateFlags{});

	auto shader_handle_storage = new uint8_t[shader_binding_table_size];
	// Get shader identifiers
	get_device().get_handle().getRayTracingShaderGroupHandlesNV(pipeline, 0, 3, shader_binding_table_size, shader_handle_storage);
	auto *data = static_cast<uint8_t *>(shader_binding_table->map());
	// Copy the shader identifiers to the shader binding table
	vk::DeviceSize offset = 0;
	data += copy_shader_identifier(data, shader_handle_storage, INDEX_RAYGEN);
	data += copy_shader_identifier(data, shader_handle_storage, INDEX_MISS);
	data += copy_shader_identifier(data, shader_handle_storage, INDEX_CLOSEST_HIT);
	shader_binding_table->unmap();
}

/*
	Create the descriptor sets used for the ray tracing dispatch
*/
void RaytracingBasic::create_descriptor_sets()
{
	std::vector<vk::DescriptorPoolSize> pool_sizes = {
	    {vk::DescriptorType::eAccelerationStructureNV, 1},
	    {vk::DescriptorType::eStorageImage, 1},
	    {vk::DescriptorType::eUniformBuffer, 1},
	};
	vk::DescriptorPoolCreateInfo descriptor_pool_create_info = vkb::initializers::descriptor_pool_create_info(pool_sizes, 1);

	descriptor_pool = get_device().get_handle().createDescriptorPool(descriptor_pool_create_info);

	vk::DescriptorSetAllocateInfo descriptor_set_allocate_info = vkb::initializers::descriptor_set_allocate_info(descriptor_pool, &descriptor_set_layout, 1);

	descriptor_set = get_device().get_handle().allocateDescriptorSets(descriptor_set_allocate_info)[0];

	vk::WriteDescriptorSetAccelerationStructureNV descriptor_acceleration_structure_info;
	descriptor_acceleration_structure_info.accelerationStructureCount = 1;
	descriptor_acceleration_structure_info.pAccelerationStructures    = &top_level_acceleration_structure.acceleration_structure;

	vk::WriteDescriptorSet acceleration_structure_write;
	// The specialized acceleration structure descriptor has to be chained
	acceleration_structure_write.pNext           = &descriptor_acceleration_structure_info;
	acceleration_structure_write.dstSet          = descriptor_set;
	acceleration_structure_write.dstBinding      = 0;
	acceleration_structure_write.descriptorCount = 1;
	acceleration_structure_write.descriptorType  = vk::DescriptorType::eAccelerationStructureNV;

	vk::DescriptorImageInfo image_descriptor;
	image_descriptor.imageView   = storage_image.view;
	image_descriptor.imageLayout = vk::ImageLayout::eGeneral;

	vk::DescriptorBufferInfo buffer_descriptor = create_descriptor(*ubo);

	vk::WriteDescriptorSet result_image_write   = vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eStorageImage, 1, &image_descriptor);
	vk::WriteDescriptorSet uniform_buffer_write = vkb::initializers::write_descriptor_set(descriptor_set, vk::DescriptorType::eUniformBuffer, 2, &buffer_descriptor);

	std::vector<vk::WriteDescriptorSet> write_descriptor_sets = {
	    acceleration_structure_write,
	    result_image_write,
	    uniform_buffer_write,
	};
	get_device().get_handle().updateDescriptorSets(write_descriptor_sets, nullptr);
}

/*
	Create our ray tracing pipeline
*/
void RaytracingBasic::create_ray_tracing_pipeline()
{
	vk::DescriptorSetLayoutBinding acceleration_structure_layout_binding;
	acceleration_structure_layout_binding.binding         = 0;
	acceleration_structure_layout_binding.descriptorType  = vk::DescriptorType::eAccelerationStructureNV;
	acceleration_structure_layout_binding.descriptorCount = 1;
	acceleration_structure_layout_binding.stageFlags      = vk::ShaderStageFlagBits::eRaygenNV;

	vk::DescriptorSetLayoutBinding result_image_layout_binding;
	result_image_layout_binding.binding         = 1;
	result_image_layout_binding.descriptorType  = vk::DescriptorType::eStorageImage;
	result_image_layout_binding.descriptorCount = 1;
	result_image_layout_binding.stageFlags      = vk::ShaderStageFlagBits::eRaygenNV;

	vk::DescriptorSetLayoutBinding uniform_buffer_binding;
	uniform_buffer_binding.binding         = 2;
	uniform_buffer_binding.descriptorType  = vk::DescriptorType::eUniformBuffer;
	uniform_buffer_binding.descriptorCount = 1;
	uniform_buffer_binding.stageFlags      = vk::ShaderStageFlagBits::eRaygenNV;

	std::vector<vk::DescriptorSetLayoutBinding> bindings({
	    acceleration_structure_layout_binding,
	    result_image_layout_binding,
	    uniform_buffer_binding,
	});

	vk::DescriptorSetLayoutCreateInfo layout_info;
	layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
	layout_info.pBindings    = bindings.data();
	descriptor_set_layout    = get_device().get_handle().createDescriptorSetLayout(layout_info);

	vk::PipelineLayoutCreateInfo pipeline_layout_create_info;
	pipeline_layout_create_info.setLayoutCount = 1;
	pipeline_layout_create_info.pSetLayouts    = &descriptor_set_layout;

	pipeline_layout = get_device().get_handle().createPipelineLayout(pipeline_layout_create_info);

	const uint32_t shader_index_raygen      = 0;
	const uint32_t shader_index_miss        = 1;
	const uint32_t shader_index_closest_hit = 2;

	std::array<vk::PipelineShaderStageCreateInfo, 3> shader_stages;
	shader_stages[shader_index_raygen]      = load_shader("nv_ray_tracing_basic/raygen.rgen", vk::ShaderStageFlagBits::eRaygenNV);
	shader_stages[shader_index_miss]        = load_shader("nv_ray_tracing_basic/miss.rmiss", vk::ShaderStageFlagBits::eMissNV);
	shader_stages[shader_index_closest_hit] = load_shader("nv_ray_tracing_basic/closesthit.rchit", vk::ShaderStageFlagBits::eClosestHitNV);

	/*
		Setup ray tracing shader groups
	*/
	std::array<vk::RayTracingShaderGroupCreateInfoNV, 3> groups{};
	for (auto &group : groups)
	{
		// Init all groups with some default values
		group.generalShader      = VK_SHADER_UNUSED_NV;
		group.closestHitShader   = VK_SHADER_UNUSED_NV;
		group.anyHitShader       = VK_SHADER_UNUSED_NV;
		group.intersectionShader = VK_SHADER_UNUSED_NV;
	}

	// Links shaders and types to ray tracing shader groups
	groups[INDEX_RAYGEN].type                  = vk::RayTracingShaderGroupTypeNV::eGeneral;
	groups[INDEX_RAYGEN].generalShader         = shader_index_raygen;
	groups[INDEX_MISS].type                    = vk::RayTracingShaderGroupTypeNV::eGeneral;
	groups[INDEX_MISS].generalShader           = shader_index_miss;
	groups[INDEX_CLOSEST_HIT].type             = vk::RayTracingShaderGroupTypeNV::eTrianglesHitGroup;
	groups[INDEX_CLOSEST_HIT].generalShader    = VK_SHADER_UNUSED_NV;
	groups[INDEX_CLOSEST_HIT].closestHitShader = shader_index_closest_hit;

	vk::RayTracingPipelineCreateInfoNV raytracing_pipeline_create_info;
	raytracing_pipeline_create_info.stageCount        = static_cast<uint32_t>(shader_stages.size());
	raytracing_pipeline_create_info.pStages           = shader_stages.data();
	raytracing_pipeline_create_info.groupCount        = static_cast<uint32_t>(groups.size());
	raytracing_pipeline_create_info.pGroups           = groups.data();
	raytracing_pipeline_create_info.maxRecursionDepth = 1;
	raytracing_pipeline_create_info.layout            = pipeline_layout;

	pipeline = get_device().get_handle().createRayTracingPipelineNV(nullptr, raytracing_pipeline_create_info);
}

/*
	Create the uniform buffer used to pass matrices to the ray tracing ray generation shader
*/
void RaytracingBasic::create_uniform_buffer()
{
	ubo = std::make_unique<vkb::core::Buffer>(get_device(),
	                                          sizeof(uniform_data),
	                                          vk::BufferUsageFlagBits::eUniformBuffer,
	                                          vma::MemoryUsage::eCpuToGpu);
	ubo->convert_and_update(uniform_data);

	update_uniform_buffers();
}

/*
	Command buffer generation
*/
void RaytracingBasic::build_command_buffers()
{
	vk::CommandBufferBeginInfo command_buffer_begin_info = vkb::initializers::command_buffer_begin_info();

	vk::ImageSubresourceRange subresource_range = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

	for (int32_t i = 0; i < draw_cmd_buffers.size(); ++i)
	{
		draw_cmd_buffers[i].begin(command_buffer_begin_info);

		/*
			Dispatch the ray tracing commands
		*/
		draw_cmd_buffers[i].bindPipeline(vk::PipelineBindPoint::eRayTracingNV, pipeline);
		draw_cmd_buffers[i].bindDescriptorSets(vk::PipelineBindPoint::eRayTracingNV, pipeline_layout, 0, descriptor_set, nullptr);

		// Calculate shader binding offsets, which is pretty straight forward in our example
		vk::DeviceSize binding_offset_ray_gen_shader = static_cast<vk::DeviceSize>(ray_tracing_properties.shaderGroupHandleSize * INDEX_RAYGEN);
		vk::DeviceSize binding_offset_miss_shader    = static_cast<vk::DeviceSize>(ray_tracing_properties.shaderGroupHandleSize * INDEX_MISS);
		vk::DeviceSize binding_offset_hit_shader     = static_cast<vk::DeviceSize>(ray_tracing_properties.shaderGroupHandleSize * INDEX_CLOSEST_HIT);
		vk::DeviceSize binding_stride                = ray_tracing_properties.shaderGroupHandleSize;

		draw_cmd_buffers[i].traceRaysNV(
		    shader_binding_table->get_handle(), binding_offset_ray_gen_shader,
		    shader_binding_table->get_handle(), binding_offset_miss_shader, binding_stride,
		    shader_binding_table->get_handle(), binding_offset_hit_shader, binding_stride,
		    nullptr, 0, 0,
		    width, height, 1);

		/*
			Copy raytracing output to swap chain image
		*/

		// Prepare current swapchain image as transfer destination
		vkb::set_image_layout(
		    draw_cmd_buffers[i],
		    get_render_context().get_swapchain().get_images()[i],
		    vk::ImageLayout::eUndefined,
		    vk::ImageLayout::eTransferDstOptimal,
		    subresource_range);

		// Prepare ray tracing output image as transfer source
		vkb::set_image_layout(
		    draw_cmd_buffers[i],
		    storage_image.image,
		    vk::ImageLayout::eGeneral,
		    vk::ImageLayout::eTransferSrcOptimal,
		    subresource_range);

		vk::ImageCopy copy_region;
		copy_region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		copy_region.srcOffset      = {0, 0, 0};
		copy_region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		copy_region.dstOffset      = {0, 0, 0};
		copy_region.extent         = {width, height, 1};
		draw_cmd_buffers[i].copyImage(storage_image.image, vk::ImageLayout::eTransferSrcOptimal,
		                              get_render_context().get_swapchain().get_images()[i], vk::ImageLayout::eTransferDstOptimal, copy_region);

		// Transition swap chain image back for presentation
		vkb::set_image_layout(
		    draw_cmd_buffers[i],
		    get_render_context().get_swapchain().get_images()[i],
		    vk::ImageLayout::eTransferDstOptimal,
		    vk::ImageLayout::ePresentSrcKHR,
		    subresource_range);

		// Transition ray tracing output image back to general layout
		vkb::set_image_layout(
		    draw_cmd_buffers[i],
		    storage_image.image,
		    vk::ImageLayout::eTransferSrcOptimal,
		    vk::ImageLayout::eGeneral,
		    subresource_range);

		draw_cmd_buffers[i].end();
	}
}

void RaytracingBasic::update_uniform_buffers()
{
	uniform_data.proj_inverse = glm::inverse(camera.matrices.perspective);
	uniform_data.view_inverse = glm::inverse(camera.matrices.view);
	ubo->convert_and_update(uniform_data);
}

bool RaytracingBasic::prepare(vkb::Platform &platform)
{
	if (!ApiVulkanSample::prepare(platform))
	{
		return false;
	}

	// This sample copies ray traced output to the swapchain image, so we need to enable the required image usage flags
	std::set<vk::ImageUsageFlagBits> image_usage_flags = {vk::ImageUsageFlagBits::eColorAttachment, vk::ImageUsageFlagBits::eTransferDst};
	get_render_context().update_swapchain(image_usage_flags);

	// Query the ray tracing properties of the current implementation, we will need them later on
	auto structure_chain   = get_device().get_physical_device().getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPropertiesNV>();
	ray_tracing_properties = structure_chain.get<vk::PhysicalDeviceRayTracingPropertiesNV>();

	// Note: Using Revsered depth-buffer for increased precision, so Znear and Zfar are flipped
	camera.type = vkb::CameraType::LookAt;
	camera.set_perspective(60.0f, (float) width / (float) height, 512.0f, 0.1f);
	camera.set_rotation(glm::vec3(0.0f, 0.0f, 0.0f));
	camera.set_translation(glm::vec3(0.0f, 0.0f, -2.5f));

	create_scene();
	create_storage_image();
	create_uniform_buffer();
	create_ray_tracing_pipeline();
	create_shader_binding_table();
	create_descriptor_sets();
	build_command_buffers();
	prepared = true;
	return true;
}

void RaytracingBasic::draw()
{
	ApiVulkanSample::prepare_frame();
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &draw_cmd_buffers[current_buffer];
	queue.submit(submit_info, nullptr);
	ApiVulkanSample::submit_frame();
}

void RaytracingBasic::render(float delta_time)
{
	if (!prepared)
		return;
	draw();
	if (camera.updated)
		update_uniform_buffers();
}

std::unique_ptr<vkb::VulkanSample> create_raytracing_basic()
{
	return std::make_unique<RaytracingBasic>();
}
