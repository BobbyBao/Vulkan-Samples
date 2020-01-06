/* Copyright (c) 2019-2020, Arm Limited and Contributors
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
#include "common/vk_common.h"

namespace vkb
{
class Device;

enum ImageFormat
{
	sRGB,
	UNORM
};

struct SwapchainProperties
{
	vk::SwapchainKHR                old_swapchain;
	uint32_t                        image_count{3};
	vk::Extent2D                    extent;
	vk::SurfaceFormatKHR            surface_format;
	uint32_t                        array_layers;
	vk::ImageUsageFlags             image_usage;
	vk::SurfaceTransformFlagBitsKHR pre_transform;
	vk::CompositeAlphaFlagBitsKHR   composite_alpha;
	vk::PresentModeKHR              present_mode;
};

class Swapchain : protected vk::SwapchainKHR
{
  public:
	/**
	 * @brief Constructor to create a swapchain by changing the extent
	 *        only and preserving the configuration from the old swapchain.
	 */
	Swapchain(Swapchain &old_swapchain, const vk::Extent2D &extent);

	/**
	 * @brief Constructor to create a swapchain by changing the image count
	 *        only and preserving the configuration from the old swapchain.
	 */
	Swapchain(Swapchain &old_swapchain, const uint32_t image_count);

	/**
	 * @brief Constructor to create a swapchain by changing the image usage
	 * only and preserving the configuration from the old swapchain.
	 */
	Swapchain(Swapchain &old_swapchain, const std::set<vk::ImageUsageFlagBits> &image_usage_flags);

	/**
	 * @brief Constructor to create a swapchain by changing the extent
	 *        and transform only and preserving the configuration from the old swapchain.
	 */
	Swapchain(Swapchain &swapchain, const vk::Extent2D &extent, const vk::SurfaceTransformFlagBitsKHR transform);

	/**
	 * @brief Constructor to create a swapchain.
	 */
	Swapchain(Device &                                device,
	          vk::SurfaceKHR                          surface,
	          const vk::Extent2D &                    extent            = {},
	          const uint32_t                          image_count       = 3,
	          const vk::SurfaceTransformFlagBitsKHR   transform         = vk::SurfaceTransformFlagBitsKHR::eIdentity,
	          const vk::PresentModeKHR                present_mode      = vk::PresentModeKHR::eFifo,
	          const std::set<vk::ImageUsageFlagBits> &image_usage_flags = {vk::ImageUsageFlagBits::eColorAttachment, vk::ImageUsageFlagBits::eTransferDst});

	/**
	 * @brief Constructor to create a swapchain from the old swapchain
	 *        by configuring all parameters.
	 */
	Swapchain(Swapchain &                             old_swapchain,
	          Device &                                device,
	          vk::SurfaceKHR                          surface,
	          const vk::Extent2D &                    extent            = {},
	          const uint32_t                          image_count       = 3,
	          const vk::SurfaceTransformFlagBitsKHR   transform         = vk::SurfaceTransformFlagBitsKHR::eIdentity,
	          const vk::PresentModeKHR                present_mode      = vk::PresentModeKHR::eFifo,
	          const std::set<vk::ImageUsageFlagBits> &image_usage_flags = {vk::ImageUsageFlagBits::eColorAttachment, vk::ImageUsageFlagBits::eTransferDst});

	Swapchain(const Swapchain &) = delete;

	Swapchain(Swapchain &&other);

	~Swapchain();

	Swapchain &operator=(const Swapchain &) = delete;

	Swapchain &operator=(Swapchain &&) = delete;

	void create();

	bool is_valid() const;

	Device &get_device();

	vk::SwapchainKHR get_handle() const;

	SwapchainProperties &get_properties();

	vk::Result acquire_next_image(uint32_t &image_index, vk::Semaphore image_acquired_semaphore, vk::Fence fence = nullptr);

	const vk::Extent2D &get_extent() const;

	vk::Format get_format() const;

	const std::vector<vk::Image> &get_images() const;

	vk::SurfaceTransformFlagsKHR get_transform() const;

	vk::SurfaceKHR get_surface() const;

	vk::ImageUsageFlags get_usage() const;

	vk::PresentModeKHR get_present_mode() const;

	/**
	 * @brief Sets the order in which the swapchain prioritizes selecting its present mode
	 */
	void set_present_mode_priority(const std::vector<vk::PresentModeKHR> &present_mode_priority_list);

	/**
	 * @brief Sets the order in which the swapchain prioritizes selecting its surface format
	 */
	void set_surface_format_priority(const std::vector<vk::SurfaceFormatKHR> &surface_format_priority_list);

  private:
	Device &device;

	vk::SurfaceKHR surface;

	std::vector<vk::Image> images;

	std::vector<vk::SurfaceFormatKHR> surface_formats{};

	std::vector<vk::PresentModeKHR> present_modes{};

	SwapchainProperties properties;

	// A list of present modes in order of priority (vector[0] has high priority, vector[size-1] has low priority)
	std::vector<vk::PresentModeKHR> present_mode_priority_list{
	    vk::PresentModeKHR::eFifo,
	    vk::PresentModeKHR::eMailbox,
	};

	// A list of surface formats in order of priority (vector[0] has high priority, vector[size-1] has low priority)
	std::vector<vk::SurfaceFormatKHR> surface_format_priority_list{
	    {vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
	    {vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
	    {vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
	    {vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
	};

	vk::PresentModeKHR present_mode;

	std::set<vk::ImageUsageFlagBits> image_usage_flags;
};
}        // namespace vkb
