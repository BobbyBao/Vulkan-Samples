/* Copyright (c) 2019, Arm Limited and Contributors
 * Copyright (c) 2019, Sascha Willems
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

#include "device.h"

VKBP_DISABLE_WARNINGS()
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
VKBP_ENABLE_WARNINGS()

namespace vkb
{
Device::Device(const vk::Instance &instance, vk::PhysicalDevice physical_device, vk::SurfaceKHR surface, const std::vector<const char *> &requested_extensions, vk::PhysicalDeviceFeatures requested_features) :
    physical_device{physical_device},
    resource_cache{*this}
{
	// Check whether ASTC is supported
	features = physical_device.getFeatures();
	if (features.textureCompressionASTC_LDR)
	{
		requested_features.textureCompressionASTC_LDR = VK_TRUE;
	}

	// Gpu properties
	properties = physical_device.getProperties();
	LOGI("GPU: {}", properties.deviceName);

	memory_properties = physical_device.getMemoryProperties();

	queue_family_properties                = physical_device.getQueueFamilyProperties();
	uint32_t queue_family_properties_count = to_u32(queue_family_properties.size());

	std::vector<vk::DeviceQueueCreateInfo> queue_create_infos(queue_family_properties_count);
	std::vector<std::vector<float>>        queue_priorities(queue_family_properties_count);

	for (uint32_t queue_family_index = 0U; queue_family_index < queue_family_properties_count; ++queue_family_index)
	{
		const vk::QueueFamilyProperties &queue_family_property = queue_family_properties[queue_family_index];

		queue_priorities[queue_family_index].resize(queue_family_property.queueCount, 1.0f);

		vk::DeviceQueueCreateInfo &queue_create_info = queue_create_infos[queue_family_index];

		queue_create_info.queueFamilyIndex = queue_family_index;
		queue_create_info.queueCount       = queue_family_property.queueCount;
		queue_create_info.pQueuePriorities = queue_priorities[queue_family_index].data();
	}

	// Check extensions to enable Vma Dedicated Allocation
	device_extensions = physical_device.enumerateDeviceExtensionProperties();

	// Display supported extensions
	if (device_extensions.size() > 0)
	{
		LOGD("Device supports the following extensions:");
		for (auto &extension : device_extensions)
		{
			LOGD("  \t{}", extension.extensionName);
		}
	}

	std::vector<const char *> supported_extensions{};

	bool can_get_memory_requirements = is_extension_supported("VK_KHR_get_memory_requirements2");
	bool has_dedicated_allocation    = is_extension_supported("VK_KHR_dedicated_allocation");

	if (can_get_memory_requirements && has_dedicated_allocation)
	{
		supported_extensions.push_back("VK_KHR_get_memory_requirements2");
		supported_extensions.push_back("VK_KHR_dedicated_allocation");
		LOGI("Dedicated Allocation enabled");
	}

	// Check that extensions are supported before trying to create the device
	std::vector<const char *> unsupported_extensions{};
	for (auto &extension : requested_extensions)
	{
		if (is_extension_supported(extension))
		{
			supported_extensions.emplace_back(extension);
		}
		else
		{
			unsupported_extensions.emplace_back(extension);
		}
	}

	if (supported_extensions.size() > 0)
	{
		LOGI("Device supports the following requested extensions:");
		for (auto &extension : supported_extensions)
		{
			LOGI("  \t{}", extension);
		}
	}

	if (unsupported_extensions.size() > 0)
	{
		LOGE("Device doesn't support the following requested extensions:");
		for (auto &extension : unsupported_extensions)
		{
			LOGE("\t{}", extension);
		}
		vk::throwResultException(vk::Result::eErrorExtensionNotPresent, "Extensions not present");
	}

	vk::DeviceCreateInfo create_info;

	create_info.pQueueCreateInfos       = queue_create_infos.data();
	create_info.queueCreateInfoCount    = to_u32(queue_create_infos.size());
	create_info.pEnabledFeatures        = &requested_features;
	create_info.enabledExtensionCount   = to_u32(supported_extensions.size());
	create_info.ppEnabledExtensionNames = supported_extensions.data();

	static_cast<vk::Device &>(*this) = physical_device.createDevice(create_info);

	volkLoadDevice(*this);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*this);

	queues.resize(queue_family_properties_count);
	for (uint32_t queue_family_index = 0U; queue_family_index < queue_family_properties_count; ++queue_family_index)
	{
		const vk::QueueFamilyProperties &queue_family_property = queue_family_properties[queue_family_index];

		vk::Bool32 present_supported{VK_FALSE};

		// Only check if surface is valid to allow for headless applications
		if (surface)
		{
			present_supported = physical_device.getSurfaceSupportKHR(queue_family_index, surface);
		}

		for (uint32_t queue_index = 0U; queue_index < queue_family_property.queueCount; ++queue_index)
		{
			queues[queue_family_index].emplace_back(*this, queue_family_index, queue_family_property, present_supported, queue_index);
		}
	}

	vma::VulkanFunctions vma_vulkan_func{};
	vma_vulkan_func.vkAllocateMemory                    = vkAllocateMemory;
	vma_vulkan_func.vkBindBufferMemory                  = vkBindBufferMemory;
	vma_vulkan_func.vkBindImageMemory                   = vkBindImageMemory;
	vma_vulkan_func.vkCreateBuffer                      = vkCreateBuffer;
	vma_vulkan_func.vkCreateImage                       = vkCreateImage;
	vma_vulkan_func.vkDestroyBuffer                     = vkDestroyBuffer;
	vma_vulkan_func.vkDestroyImage                      = vkDestroyImage;
	vma_vulkan_func.vkFlushMappedMemoryRanges           = vkFlushMappedMemoryRanges;
	vma_vulkan_func.vkFreeMemory                        = vkFreeMemory;
	vma_vulkan_func.vkGetBufferMemoryRequirements       = vkGetBufferMemoryRequirements;
	vma_vulkan_func.vkGetImageMemoryRequirements        = vkGetImageMemoryRequirements;
	vma_vulkan_func.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
	vma_vulkan_func.vkGetPhysicalDeviceProperties       = vkGetPhysicalDeviceProperties;
	vma_vulkan_func.vkInvalidateMappedMemoryRanges      = vkInvalidateMappedMemoryRanges;
	vma_vulkan_func.vkMapMemory                         = vkMapMemory;
	vma_vulkan_func.vkUnmapMemory                       = vkUnmapMemory;

	vma::Allocator::CreateInfo allocator_info;
	allocator_info.physicalDevice = physical_device;
	allocator_info.device         = operator VkDevice();

	if (can_get_memory_requirements && has_dedicated_allocation)
	{
		allocator_info.flags |= vma::Allocator::CreateFlagBits::eDedicatedAllocation;
		vma_vulkan_func.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR;
		vma_vulkan_func.vkGetImageMemoryRequirements2KHR  = vkGetImageMemoryRequirements2KHR;
	}

	allocator_info.pVulkanFunctions = &vma_vulkan_func;

	memory_allocator = vma::createAllocator(allocator_info);

	const auto &primary_queue_reference = get_queue_by_flags(vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, 0);
	primary_queue                       = &primary_queue_reference;

	command_pool = std::make_unique<CommandPool>(*this, primary_queue_reference.get_family_index());
	fence_pool   = std::make_unique<FencePool>(*this);
}

Device::~Device()
{
	resource_cache.clear();

	command_pool.reset();
	fence_pool.reset();

	if (memory_allocator)
	{
		auto stats = memory_allocator.calculateStats();
		LOGI("Total device memory leaked: {} bytes.", stats.total.usedBytes);
		memory_allocator.destroy();
	}

	if (operator bool())
	{
		destroy();
	}
}

bool Device::is_extension_supported(const std::string &requested_extension)
{
	return std::find_if(device_extensions.begin(), device_extensions.end(),
	                    [requested_extension](auto &device_extension) {
		                    return std::strcmp(device_extension.extensionName, requested_extension.c_str()) == 0;
	                    }) != device_extensions.end();
}

vk::PhysicalDevice Device::get_physical_device() const
{
	return physical_device;
}

const vk::PhysicalDeviceFeatures &Device::get_features() const
{
	return features;
}

vk::Device Device::get_handle() const
{
	return static_cast<const vk::Device &>(*this);
}

const vma::Allocator &Device::get_memory_allocator() const
{
	return memory_allocator;
}

const vk::PhysicalDeviceProperties &Device::get_properties() const
{
	return properties;
}

DriverVersion Device::get_driver_version() const
{
	DriverVersion version;

	switch (properties.vendorID)
	{
		case 0x10DE:
		{
			// Nvidia
			version.major = (properties.driverVersion >> 22) & 0x3ff;
			version.minor = (properties.driverVersion >> 14) & 0x0ff;
			version.patch = (properties.driverVersion >> 6) & 0x0ff;
			// Ignoring optional tertiary info in lower 6 bits
			break;
		}
		default:
		{
			version.major = VK_VERSION_MAJOR(properties.driverVersion);
			version.minor = VK_VERSION_MINOR(properties.driverVersion);
			version.patch = VK_VERSION_PATCH(properties.driverVersion);
		}
	}

	return version;
}

bool Device::is_image_format_supported(vk::Format format) const
{
	vk::ImageFormatProperties format_properties;
	auto                      result = vkGetPhysicalDeviceImageFormatProperties(physical_device,
                                                           static_cast<VkFormat>(format),
                                                           static_cast<VkImageType>(vk::ImageType::e2D),
                                                           static_cast<VkImageTiling>(vk::ImageTiling::eOptimal),
                                                           static_cast<VkImageUsageFlags>(vk::ImageUsageFlagBits::eSampled),
                                                           0,        // no create flags
                                                           &(format_properties.operator VkImageFormatProperties &()));
	return result != VK_ERROR_FORMAT_NOT_SUPPORTED;
}

uint32_t Device::get_memory_type(uint32_t bits, vk::MemoryPropertyFlags properties, vk::Bool32 *memory_type_found)
{
	for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
	{
		if ((bits & 1) == 1)
		{
			if ((memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
			{
				if (memory_type_found)
				{
					*memory_type_found = true;
				}
				return i;
			}
		}
		bits >>= 1;
	}

	if (memory_type_found)
	{
		*memory_type_found = false;
		return 0;
	}
	else
	{
		throw std::runtime_error("Could not find a matching memory type");
	}
}

const vk::FormatProperties Device::get_format_properties(vk::Format format) const
{
	return physical_device.getFormatProperties(format);
	;
}

const Queue &Device::get_queue(uint32_t queue_family_index, uint32_t queue_index)
{
	return queues[queue_family_index][queue_index];
}

const Queue &Device::get_queue_by_flags(vk::QueueFlags required_queue_flags, uint32_t queue_index)
{
	for (uint32_t queue_family_index = 0U; queue_family_index < queues.size(); ++queue_family_index)
	{
		Queue &first_queue = queues[queue_family_index][0];

		vk::QueueFlags queue_flags = first_queue.get_properties().queueFlags;
		uint32_t       queue_count = first_queue.get_properties().queueCount;

		if (((queue_flags & required_queue_flags) == required_queue_flags) && queue_index < queue_count)
		{
			return queues[queue_family_index][queue_index];
		}
	}

	throw std::runtime_error("Queue not found");
}

const Queue &Device::get_queue_by_present(uint32_t queue_index)
{
	for (uint32_t queue_family_index = 0U; queue_family_index < queues.size(); ++queue_family_index)
	{
		Queue &first_queue = queues[queue_family_index][0];

		uint32_t queue_count = first_queue.get_properties().queueCount;

		if (first_queue.support_present() && queue_index < queue_count)
		{
			return queues[queue_family_index][queue_index];
		}
	}

	throw std::runtime_error("Queue not found");
}

uint32_t Device::get_queue_family_index(vk::QueueFlags queue_flag)
{
	// Dedicated queue for compute
	// Try to find a queue family index that supports compute but not graphics
	if (queue_flag & vk::QueueFlagBits::eCompute)
	{
		for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++)
		{
			if ((queue_family_properties[i].queueFlags & queue_flag) && (!(queue_family_properties[i].queueFlags & vk::QueueFlagBits::eGraphics)))
			{
				return i;
				break;
			}
		}
	}

	// Dedicated queue for transfer
	// Try to find a queue family index that supports transfer but not graphics and compute
	if (queue_flag & vk::QueueFlagBits::eTransfer)
	{
		for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++)
		{
			if ((queue_family_properties[i].queueFlags & queue_flag) && (!(queue_family_properties[i].queueFlags & vk::QueueFlagBits::eGraphics)) && (!(queue_family_properties[i].queueFlags & vk::QueueFlagBits::eCompute)))
			{
				return i;
				break;
			}
		}
	}

	// For other queue types or if no separate compute queue is present, return the first one to support the requested flags
	for (uint32_t i = 0; i < static_cast<uint32_t>(queue_family_properties.size()); i++)
	{
		if (queue_family_properties[i].queueFlags & queue_flag)
		{
			return i;
			break;
		}
	}

	throw std::runtime_error("Could not find a matching queue family index");
}

const Queue &Device::get_suitable_graphics_queue()
{
	for (uint32_t queue_family_index = 0U; queue_family_index < queues.size(); ++queue_family_index)
	{
		Queue &first_queue = queues[queue_family_index][0];

		uint32_t queue_count = first_queue.get_properties().queueCount;

		if (first_queue.support_present() && 0 < queue_count)
		{
			return queues[queue_family_index][0];
		}
	}

	return get_queue_by_flags(vk::QueueFlagBits::eGraphics, 0);
}

vk::Buffer Device::create_buffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::DeviceSize size, vk::DeviceMemory *memory, void *data)
{
	// Create the buffer handle
	vk::Buffer buffer = createBuffer({{}, size, usage});

	// Create the memory backing up the buffer handle
	vk::MemoryRequirements memory_requirements = getBufferMemoryRequirements(buffer);

	vk::MemoryAllocateInfo memory_allocation;
	memory_allocation.allocationSize = memory_requirements.size;
	// Find a memory type index that fits the properties of the buffer
	memory_allocation.memoryTypeIndex = get_memory_type(memory_requirements.memoryTypeBits, properties);
	*memory                           = allocateMemory(memory_allocation);

	// If a pointer to the buffer data has been passed, map the buffer and copy over the
	if (data != nullptr)
	{
		void *mapped = mapMemory(*memory, 0, VK_WHOLE_SIZE);
		memcpy(mapped, data, static_cast<size_t>(size));
		// If host coherency hasn't been requested, do a manual flush to make writes visible
		if (!(properties & vk::MemoryPropertyFlagBits::eHostCoherent))
		{
			vk::MappedMemoryRange mapped_range;
			mapped_range.memory = *memory;
			mapped_range.offset = 0;
			mapped_range.size   = size;
			flushMappedMemoryRanges(mapped_range);
		}
		unmapMemory(*memory);
	}

	// Attach the memory to the buffer object
	bindBufferMemory(buffer, *memory, 0);

	return buffer;
}

void Device::copy_buffer(vkb::core::Buffer &src, vkb::core::Buffer &dst, vk::Queue queue, vk::BufferCopy *copy_region)
{
	assert(dst.get_size() <= src.get_size());
	assert(src.get_handle());

	vk::CommandBuffer command_buffer = create_command_buffer(vk::CommandBufferLevel::ePrimary, true);

	vk::BufferCopy buffer_copy;
	if (copy_region == nullptr)
	{
		buffer_copy.size = src.get_size();
	}
	else
	{
		buffer_copy = *copy_region;
	}

	command_buffer.copyBuffer(src.get_handle(), dst.get_handle(), buffer_copy);

	flush_command_buffer(command_buffer, queue);
}

vk::CommandPool Device::create_command_pool(uint32_t queue_index, vk::CommandPoolCreateFlags flags)
{
	return createCommandPool({flags, queue_index});
}

vk::CommandBuffer Device::create_command_buffer(vk::CommandBufferLevel level, bool begin)
{
	assert(command_pool && "No command pool exists in the device");

	vk::CommandBuffer command_buffer = allocateCommandBuffers({command_pool->get_handle(), level, 1})[0];

	// If requested, also start recording for the new command buffer
	if (begin)
	{
		command_buffer.begin(vk::CommandBufferBeginInfo{});
	}

	return command_buffer;
}

void Device::flush_command_buffer(vk::CommandBuffer command_buffer, vk::Queue queue, bool free)
{
	if (!command_buffer)
	{
		return;
	}

	command_buffer.end();

	// Create fence to ensure that the command buffer has finished executing
	vk::Fence fence = fence_pool->request_fence();

	// Submit to the queue
	vk::SubmitInfo submit_info;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers    = &command_buffer;
	queue.submit(submit_info, fence);

	// Wait for the fence to signal that command buffer has finished executing
	fence_pool->wait();
	fence_pool->reset();

	if (command_pool && free)
	{
		freeCommandBuffers(command_pool->get_handle(), command_buffer);
	}
}

CommandPool &Device::get_command_pool()
{
	return *command_pool;
}

FencePool &Device::get_fence_pool()
{
	return *fence_pool;
}

CommandBuffer &Device::request_command_buffer()
{
	return command_pool->request_command_buffer();
}

vk::Fence Device::request_fence()
{
	return fence_pool->request_fence();
}

vk::Result Device::wait_idle()
{
	waitIdle();
	return vk::Result::eSuccess;
}

ResourceCache &Device::get_resource_cache()
{
	return resource_cache;
}

void Device::with_command_buffer(const std::function<void(const vk::CommandBuffer &)> &f)
{
	auto &command_buffer = create_command_buffer(vk::CommandBufferLevel::ePrimary);
	command_buffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	f(command_buffer);
	flush_command_buffer(command_buffer, primary_queue->get_handle(), true);
}

core::Buffer Device::stage_to_device_buffer(const void *data, vk::DeviceSize size, const vk::BufferUsageFlags &usage_flags)
{
	auto result = core::Buffer{
	    *this,
	    size,
	    usage_flags | vk::BufferUsageFlagBits::eTransferDst,
	    vma::MemoryUsage::eGpuOnly};

	core::Buffer stage_buffer{
	    *this,
	    size,
	    vk::BufferUsageFlagBits::eTransferSrc,
	    vma::MemoryUsage::eCpuOnly};

	stage_buffer.update(static_cast<const uint8_t *>(data), size);
	with_command_buffer([&](const vk::CommandBuffer &command_buffer) {
		command_buffer.copyBuffer(stage_buffer.get_handle(), result.get_handle(), vk::BufferCopy{0, 0, size});
	});

	return result;
};

void Device::stage_to_image(
    const void *                            data,
    vk::DeviceSize                          size,
    const std::vector<vk::BufferImageCopy> &regions,
    const core::Image &                     image)
{
	core::Buffer stage_buffer{
	    *this,
	    size,
	    vk::BufferUsageFlagBits::eTransferSrc,
	    vma::MemoryUsage::eCpuOnly};

	stage_buffer.update(static_cast<const uint8_t *>(data), size);
	const vk::ImageSubresource &subresource = image.get_subresource();

	vk::ImageSubresourceRange subresource_range{
	    subresource.aspectMask,
	    0, subresource.mipLevel,
	    0, subresource.arrayLayer};

	with_command_buffer([&](const vk::CommandBuffer &command_buffer) {
		// Prepare for transfer
		vkb::insert_image_memory_barrier(
		    command_buffer, image.get_handle(),
		    {}, vk::AccessFlagBits::eTransferWrite,
		    vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
		    vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer,
		    subresource_range);

		// Copy
		command_buffer.copyBufferToImage(stage_buffer.get_handle(), image.get_handle(), vk::ImageLayout::eTransferDstOptimal, regions);

		// Prepare for fragmen shader
		vkb::insert_image_memory_barrier(
		    command_buffer, image.get_handle(),
		    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
		    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
		    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
		    subresource_range);
	});
}

void Device::stage_to_image(
    const void *       data,
    vk::DeviceSize     size,
    const core::Image &image)
{
	const vk::ImageSubresource &subresource = image.get_subresource();
	auto                        copy_region = vk::BufferImageCopy{0, 0, 0, {subresource.aspectMask, 0, 0, subresource.arrayLayer}, {}, image.get_extent()};
	stage_to_image(data, size, {copy_region}, image);
}

core::Image Device::stage_to_device_image(
    const void *            data,
    vk::DeviceSize          size,
    const vk::Extent3D &    extent,
    vk::Format              format,
    vk::ImageUsageFlags     image_usage,
    vma::MemoryUsage        memory_usage,
    vk::SampleCountFlagBits sample_count,
    uint32_t                mip_levels,
    uint32_t                array_layers,
    vk::ImageTiling         tiling,
    vk::ImageCreateFlags    flags)

{
	auto result = core::Image{
	    *this,
	    extent, format,
	    image_usage,
	    memory_usage,
	    sample_count,
	    mip_levels,
	    array_layers,
	    tiling, flags};
	stage_to_image(data, size, result);
	return result;
}

}        // namespace vkb
