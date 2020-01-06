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

#pragma once

#include "common/helpers.h"
#include "common/logging.h"
#include "common/vk_common.h"
#include "core/command_buffer.h"
#include "core/command_pool.h"
#include "core/descriptor_set.h"
#include "core/descriptor_set_layout.h"
#include "core/framebuffer.h"
#include "core/instance.h"
#include "core/pipeline.h"
#include "core/pipeline_layout.h"
#include "core/queue.h"
#include "core/render_pass.h"
#include "core/shader_module.h"
#include "core/swapchain.h"
#include "fence_pool.h"
#include "rendering/pipeline_state.h"
#include "rendering/render_target.h"
#include "resource_cache.h"

namespace vkb
{
struct DriverVersion
{
	uint16_t major;
	uint16_t minor;
	uint16_t patch;
};

class Device : protected vk::Device
{
  public:
	Device(const vk::Instance &instance, vk::PhysicalDevice physical_device, vk::SurfaceKHR surface, const std::vector<const char *> &requested_extensions = {}, vk::PhysicalDeviceFeatures features = {});

	Device(const Device &) = delete;

	Device(Device &&) = delete;

	~Device();

	Device &operator=(const Device &) = delete;

	Device &operator=(Device &&) = delete;

	vk::PhysicalDevice get_physical_device() const;

	const vk::PhysicalDeviceFeatures &get_features() const;

	vk::Device get_handle() const;

	const vma::Allocator &get_memory_allocator() const;

	const vk::PhysicalDeviceProperties &get_properties() const;

	/**
	 * @return The version of the driver of the current physical device
	 */
	DriverVersion get_driver_version() const;

	/**
	 * @return Whether an image format is supported by the GPU
	 */
	bool is_image_format_supported(vk::Format format) const;

	const vk::FormatProperties get_format_properties(vk::Format format) const;

	const Queue &get_queue(uint32_t queue_family_index, uint32_t queue_index);

	const Queue &get_queue_by_flags(vk::QueueFlags queue_flags, uint32_t queue_index);

	const Queue &get_queue_by_present(uint32_t queue_index);

	/**
	 * @brief Finds a suitable graphics queue to submit to
	 * @return The first present supported queue, otherwise just any graphics queue
	 */
	const Queue &get_suitable_graphics_queue();

	bool is_extension_supported(const std::string &extension);

	uint32_t get_queue_family_index(vk::QueueFlags queue_flag);

	CommandPool &get_command_pool();

	/**
	 * @brief Checks that a given memory type is supported by the GPU
	 * @param bits The memory requirement type bits
	 * @param properties The memory property to search for
	 * @param memory_type_found True if found, false if not found
	 * @returns The memory type index of the found memory type
	 */
	uint32_t get_memory_type(uint32_t bits, vk::MemoryPropertyFlags properties, vk::Bool32 *memory_type_found = nullptr);

	/**
	* @brief Creates a vulkan buffer
	* @param usage The buffer usage
	* @param properties The memory properties
	* @param size The size of the buffer
	* @param memory The pointer to the buffer memory
	* @param data The data to place inside the buffer
	* @returns A valid vk::Buffer
	*/
	vk::Buffer create_buffer(vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::DeviceSize size, vk::DeviceMemory *memory, void *data = nullptr);

	/**
	* @brief Copies a buffer from one to another
	* @param src The buffer to copy from
	* @param dst The buffer to copy to
	* @param queue The queue to submit the copy command to
	* @param copy_region The amount to copy, if null copies the entire buffer
	*/
	void copy_buffer(vkb::core::Buffer &src, vkb::core::Buffer &dst, vk::Queue queue, vk::BufferCopy *copy_region = nullptr);

	/**
	 * @brief Creates a command pool
	 * @param queue_index The queue index this command pool is associated with
	 * @param flags The command pool flags
	 * @returns A valid vk::CommandPool
	 */
	vk::CommandPool create_command_pool(uint32_t queue_index, vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer);

	/**
	 * @brief Requests a command buffer from the device's command pool
	 * @param level The command buffer level
	 * @param begin Whether the command buffer should be implictly started before it's returned
	 * @returns A valid vk::CommandBuffer
	 */
	vk::CommandBuffer create_command_buffer(vk::CommandBufferLevel level, bool begin = false);

	/**
	 * @brief Submits and frees up a given command buffer
	 * @param command_buffer The command buffer
	 * @param queue The queue to submit the work to
	 * @param free Whether the command buffer should be implictly freed up
	 */
	void flush_command_buffer(vk::CommandBuffer command_buffer, vk::Queue queue, bool free = true);

	/**
	 * @brief Requests a command buffer from the general command_pool
	 * @return A new command buffer
	 */
	CommandBuffer &request_command_buffer();

	void with_command_buffer(const std::function<void(const vk::CommandBuffer &)> &f);

	void stage_to_image(
	    const void *       data,
	    vk::DeviceSize     size,
	    const core::Image &image);

	template <typename T>
	void stage_to_image(
	    const std::vector<T> &data,
	    const core::Image &   image)
	{
		stage_to_device_image(data.data(), data.size() * sizeof(T), image);
	}

	void stage_to_image(
	    const void *                            data,
	    vk::DeviceSize                          size,
	    const std::vector<vk::BufferImageCopy> &regions,
	    const core::Image &                     image);

	template <typename T>
	void stage_to_image(
	    const std::vector<T> &                  data,
	    const std::vector<vk::BufferImageCopy> &regions,
	    const core::Image &                     image)
	{
		stage_to_image(data.data(), data.size() * sizeof(T), regions, image);
	}

	core::Image stage_to_device_image(
	    const void *            data,
	    vk::DeviceSize          size,
	    const vk::Extent3D &    extent,
	    vk::Format              format,
	    vk::ImageUsageFlags     image_usage,
	    vma::MemoryUsage        memory_usage,
	    vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1,
	    uint32_t                mip_levels   = 1,
	    uint32_t                array_layers = 1,
	    vk::ImageTiling         tiling       = vk::ImageTiling::eOptimal,
	    vk::ImageCreateFlags    flags        = {});

	template <typename T>
	core::Image stage_to_device_image(
	    const std::vector<T> &  data,
	    const vk::Extent3D &    extent,
	    vk::Format              format,
	    vk::ImageUsageFlags     image_usage,
	    vma::MemoryUsage        memory_usage,
	    vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1,
	    uint32_t                mip_levels   = 1,
	    uint32_t                array_layers = 1,
	    vk::ImageTiling         tiling       = vk::ImageTiling::eOptimal,
	    vk::ImageCreateFlags    flags        = {})
	{
		return stage_to_device_image(data.data(), data.size() * sizeof(T), extent, format, image_usage, sample_count, mip_levels, array_layers, tiling, flags);
	}

	core::Image stage_to_device_image(
	    const void *                            data,
	    vk::DeviceSize                          size,
	    const std::vector<vk::BufferImageCopy> &regions,
	    const vk::Extent3D &                    extent,
	    vk::Format                              format,
	    vk::ImageUsageFlags                     image_usage,
	    vma::MemoryUsage                        memory_usage,
	    vk::SampleCountFlagBits                 sample_count = vk::SampleCountFlagBits::e1,
	    uint32_t                                mip_levels   = 1,
	    uint32_t                                array_layers = 1,
	    vk::ImageTiling                         tiling       = vk::ImageTiling::eOptimal,
	    vk::ImageCreateFlags                    flags        = {});

	template <typename T>
	core::Image stage_to_device_image(
	    const std::vector<T> &                  data,
	    const std::vector<vk::BufferImageCopy> &regions,
	    const vk::Extent3D &                    extent,
	    vk::Format                              format,
	    vk::ImageUsageFlags                     image_usage,
	    vma::MemoryUsage                        memory_usage,
	    vk::SampleCountFlagBits                 sample_count = vk::SampleCountFlagBits::e1,
	    uint32_t                                mip_levels   = 1,
	    uint32_t                                array_layers = 1,
	    vk::ImageTiling                         tiling       = vk::ImageTiling::eOptimal,
	    vk::ImageCreateFlags                    flags        = {})
	{
		return stage_to_device_image(data.data(), data.size() * sizeof(T), regions, extent, format, image_usage, sample_count, mip_levels, array_layers, tiling, flags);
	}

	core::Buffer stage_to_device_buffer(const void *data, vk::DeviceSize size, const vk::BufferUsageFlags &usage_flags);

	template <typename T>
	core::Buffer stage_to_device_buffer(const T &data, const vk::BufferUsageFlags &usage_flags)
	{
		return stage_to_device_buffer(&data, sizeof(T), usage_flags);
	}

	template <typename T>
	core::Buffer stage_to_device_buffer(const std::vector<T> &data, const vk::BufferUsageFlags &usage_flags)
	{
		return stage_to_device_buffer(data.data(), data.size() * sizeof(T), usage_flags);
	}

	FencePool &get_fence_pool();

	/**
	 * @brief Requests a fence to the fence pool
	 * @return A vulkan fence
	 */
	vk::Fence request_fence();

	vk::Result wait_idle();

	ResourceCache &get_resource_cache();

  private:
	std::vector<vk::ExtensionProperties> device_extensions;

	vk::PhysicalDevice physical_device;

	vk::PhysicalDeviceFeatures features;

	vk::SurfaceKHR surface;

	uint32_t queue_family_count{0};

	std::vector<vk::QueueFamilyProperties> queue_family_properties;

	vma::Allocator memory_allocator;

	vk::PhysicalDeviceProperties properties;

	vk::PhysicalDeviceMemoryProperties memory_properties;

	std::vector<std::vector<Queue>> queues;

	const Queue *primary_queue{nullptr};
	/// A command pool associated to the primary queue
	std::unique_ptr<CommandPool> command_pool;

	/// A fence pool associated to the primary queue
	std::unique_ptr<FencePool> fence_pool;

	ResourceCache resource_cache;
};
}        // namespace vkb
