/* Copyright (c) 2019, Arm Limited and Contributors
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

#include "core/swapchain.h"

#include "common/logging.h"
#include "device.h"

namespace vkb
{
namespace
{
inline uint32_t choose_image_count(
    uint32_t request_image_count,
    uint32_t min_image_count,
    uint32_t max_image_count)
{
	if (max_image_count != 0)
	{
		request_image_count = std::min(request_image_count, max_image_count);
	}

	request_image_count = std::max(request_image_count, min_image_count);

	return request_image_count;
}

inline uint32_t choose_image_array_layers(
    uint32_t request_image_array_layers,
    uint32_t max_image_array_layers)
{
	request_image_array_layers = std::min(request_image_array_layers, max_image_array_layers);
	request_image_array_layers = std::max(request_image_array_layers, 1u);

	return request_image_array_layers;
}

inline vk::Extent2D choose_extent(
    vk::Extent2D        request_extent,
    const vk::Extent2D &min_image_extent,
    const vk::Extent2D &max_image_extent,
    const vk::Extent2D &current_extent)
{
	if (request_extent.width < 1 || request_extent.height < 1)
	{
		LOGW("(Swapchain) Image extent ({}, {}) not supported. Selecting ({}, {}).", request_extent.width, request_extent.height, current_extent.width, current_extent.height);
		return current_extent;
	}

	request_extent.width = std::max(request_extent.width, min_image_extent.width);
	request_extent.width = std::min(request_extent.width, max_image_extent.width);

	request_extent.height = std::max(request_extent.height, min_image_extent.height);
	request_extent.height = std::min(request_extent.height, max_image_extent.height);

	return request_extent;
}

inline vk::PresentModeKHR choose_present_mode(
    vk::PresentModeKHR                     request_present_mode,
    const std::vector<vk::PresentModeKHR> &available_present_modes,
    const std::vector<vk::PresentModeKHR> &present_mode_priority_list)
{
	auto present_mode_it = std::find(available_present_modes.begin(), available_present_modes.end(), request_present_mode);

	if (present_mode_it == available_present_modes.end())
	{
		// If nothing found, always default to FIFO
		vk::PresentModeKHR chosen_present_mode = vk::PresentModeKHR::eFifo;

		for (auto &present_mode : present_mode_priority_list)
		{
			if (std::find(available_present_modes.begin(), available_present_modes.end(), present_mode) != available_present_modes.end())
			{
				chosen_present_mode = present_mode;
			}
		}

		LOGW("(Swapchain) Present mode '{}' not supported. Selecting '{}'.", vk::to_string(request_present_mode), vk::to_string(chosen_present_mode));
		return chosen_present_mode;
	}
	else
	{
		LOGI("(Swapchain) Present mode selected: {}", vk::to_string(request_present_mode));
		return *present_mode_it;
	}
}

inline vk::SurfaceFormatKHR choose_surface_format(
    const vk::SurfaceFormatKHR               requested_surface_format,
    const std::vector<vk::SurfaceFormatKHR> &available_surface_formats,
    const std::vector<vk::SurfaceFormatKHR> &surface_format_priority_list)
{
	// Try to find the requested surface format in the supported surface formats
	auto surface_format_it = std::find(
	    available_surface_formats.begin(),
	    available_surface_formats.end(),
	    requested_surface_format);

	// If the requested surface format isn't found, then try to request a format from the priority list
	if (surface_format_it == available_surface_formats.end())
	{
		for (auto &surface_format : surface_format_priority_list)
		{
			surface_format_it = std::find(
			    available_surface_formats.begin(),
			    available_surface_formats.end(),
			    surface_format);

			if (surface_format_it != available_surface_formats.end())
			{
				LOGW("(Swapchain) Surface format ({}) not supported. Selecting ({}).", to_string(requested_surface_format), to_string(*surface_format_it));
				return *surface_format_it;
			}
		}

		// If nothing found, default the first supporte surface format
		surface_format_it = available_surface_formats.begin();
		LOGW("(Swapchain) Surface format ({}) not supported. Selecting ({}).", to_string(requested_surface_format), to_string(*surface_format_it));
	}
	else
	{
		LOGI("(Swapchain) Surface format selected: {}", to_string(requested_surface_format));
	}

	return *surface_format_it;
}

inline vk::SurfaceTransformFlagBitsKHR choose_transform(
    vk::SurfaceTransformFlagBitsKHR request_transform,
    vk::SurfaceTransformFlagsKHR    supported_transform,
    vk::SurfaceTransformFlagBitsKHR current_transform)
{
	if (request_transform & supported_transform)
	{
		return request_transform;
	}

	LOGW("(Swapchain) Surface transform '{}' not supported. Selecting '{}'.", vk::to_string(request_transform), vk::to_string(current_transform));

	return current_transform;
}

inline vk::CompositeAlphaFlagBitsKHR choose_composite_alpha(vk::CompositeAlphaFlagBitsKHR request_composite_alpha, vk::CompositeAlphaFlagsKHR supported_composite_alpha)
{
	if (request_composite_alpha & supported_composite_alpha)
	{
		return request_composite_alpha;
	}

	static const std::vector<vk::CompositeAlphaFlagBitsKHR> composite_alpha_flags{
	    vk::CompositeAlphaFlagBitsKHR::eOpaque,
	    vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
	    vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
	    vk::CompositeAlphaFlagBitsKHR::eInherit,
	};

	for (vk::CompositeAlphaFlagBitsKHR composite_alpha : composite_alpha_flags)
	{
		if (composite_alpha & supported_composite_alpha)
		{
			LOGW("(Swapchain) Composite alpha '{}' not supported. Selecting '{}.", vk::to_string(request_composite_alpha), vk::to_string(composite_alpha));
			return composite_alpha;
		}
	}

	throw std::runtime_error("No compatible composite alpha found.");
}

inline bool validate_format_feature(vk::ImageUsageFlagBits image_usage, vk::FormatFeatureFlags supported_features)
{
	switch (image_usage)
	{
		case vk::ImageUsageFlagBits::eStorage:
			return (vk::FormatFeatureFlagBits::eStorageImage & supported_features).operator bool();
		default:
			return true;
	}
}

inline std::set<vk::ImageUsageFlagBits> choose_image_usage(const std::set<vk::ImageUsageFlagBits> &requested_image_usage_flags, vk::ImageUsageFlags supported_image_usage, vk::FormatFeatureFlags supported_features)
{
	std::set<vk::ImageUsageFlagBits> validated_image_usage_flags;
	for (auto flag : requested_image_usage_flags)
	{
		if ((flag & supported_image_usage) && validate_format_feature(flag, supported_features))
		{
			validated_image_usage_flags.insert(flag);
		}
		else
		{
			LOGW("(Swapchain) Image usage ({}) requested but not supported.", vk::to_string(flag));
		}
	}

	if (validated_image_usage_flags.empty())
	{
		// Pick the first format from list of defaults, if supported
		static const std::vector<vk::ImageUsageFlagBits> image_usage_flags = {
		    vk::ImageUsageFlagBits::eColorAttachment,
		    vk::ImageUsageFlagBits::eStorage,
		    vk::ImageUsageFlagBits::eSampled,
		    vk::ImageUsageFlagBits::eTransferDst};

		for (vk::ImageUsageFlagBits image_usage : image_usage_flags)
		{
			if ((image_usage & supported_image_usage) && validate_format_feature(image_usage, supported_features))
			{
				validated_image_usage_flags.insert(image_usage);
				break;
			}
		}
	}

	if (!validated_image_usage_flags.empty())
	{
		// Log image usage flags used
		std::string usage_list;
		for (vk::ImageUsageFlagBits image_usage : validated_image_usage_flags)
		{
			usage_list += vk::to_string(image_usage) + " ";
		}
		LOGI("(Swapchain) Image usage flags: {}", usage_list);
	}
	else
	{
		throw std::runtime_error("No compatible image usage found.");
	}

	return validated_image_usage_flags;
}

inline vk::ImageUsageFlags composite_image_flags(std::set<vk::ImageUsageFlagBits> &image_usage_flags)
{
	vk::ImageUsageFlags image_usage;
	for (auto flag : image_usage_flags)
	{
		image_usage |= flag;
	}
	return image_usage;
}

}        // namespace

Swapchain::Swapchain(Swapchain &old_swapchain, const vk::Extent2D &extent) :
    Swapchain{old_swapchain, old_swapchain.device, old_swapchain.surface, extent, old_swapchain.properties.image_count, old_swapchain.properties.pre_transform, old_swapchain.properties.present_mode, old_swapchain.image_usage_flags}
{
	present_mode_priority_list = old_swapchain.present_mode_priority_list;
	//	surface_format_priority_list = old_swapchain.surface_format_priority_list;
	create();
}

Swapchain::Swapchain(Swapchain &old_swapchain, const uint32_t image_count) :
    Swapchain{old_swapchain, old_swapchain.device, old_swapchain.surface, old_swapchain.properties.extent, image_count, old_swapchain.properties.pre_transform, old_swapchain.properties.present_mode, old_swapchain.image_usage_flags}
{
	present_mode_priority_list = old_swapchain.present_mode_priority_list;
	//	surface_format_priority_list = old_swapchain.surface_format_priority_list;
	create();
}

Swapchain::Swapchain(Swapchain &old_swapchain, const std::set<vk::ImageUsageFlagBits> &image_usage_flags) :
    Swapchain{old_swapchain, old_swapchain.device, old_swapchain.surface, old_swapchain.properties.extent, old_swapchain.properties.image_count, old_swapchain.properties.pre_transform, old_swapchain.properties.present_mode, image_usage_flags}
{
	present_mode_priority_list = old_swapchain.present_mode_priority_list;
	//	surface_format_priority_list = old_swapchain.surface_format_priority_list;
	create();
}

Swapchain::Swapchain(Swapchain &old_swapchain, const vk::Extent2D &extent, const vk::SurfaceTransformFlagBitsKHR transform) :
    Swapchain{old_swapchain, old_swapchain.device, old_swapchain.surface, extent, old_swapchain.properties.image_count, transform, old_swapchain.properties.present_mode, old_swapchain.image_usage_flags}
{
	present_mode_priority_list = old_swapchain.present_mode_priority_list;
	//	surface_format_priority_list = old_swapchain.surface_format_priority_list;
	create();
}

Swapchain::Swapchain(Device &                                device,
                     vk::SurfaceKHR                          surface,
                     const vk::Extent2D &                    extent,
                     const uint32_t                          image_count,
                     const vk::SurfaceTransformFlagBitsKHR   transform,
                     const vk::PresentModeKHR                present_mode,
                     const std::set<vk::ImageUsageFlagBits> &image_usage_flags) :
    Swapchain{*this, device, surface, extent, image_count, transform, present_mode, image_usage_flags}
{
}

Swapchain::Swapchain(Swapchain &                             old_swapchain,
                     Device &                                device,
                     vk::SurfaceKHR                          surface,
                     const vk::Extent2D &                    extent,
                     const uint32_t                          image_count,
                     const vk::SurfaceTransformFlagBitsKHR   transform,
                     const vk::PresentModeKHR                present_mode,
                     const std::set<vk::ImageUsageFlagBits> &image_usage_flags) :
    device{device},
    surface{surface}
{
	present_mode_priority_list = old_swapchain.present_mode_priority_list;
	//surface_format_priority_list = old_swapchain.surface_format_priority_list;

	auto                       physical_device      = this->device.get_physical_device();
	vk::SurfaceCapabilitiesKHR surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);

	surface_formats = physical_device.getSurfaceFormatsKHR(surface);
	LOGI("Surface supports the following surface formats:");
	for (auto &surface_format : surface_formats)
	{
		LOGI("  \t{}", to_string(surface_format));
	}

	present_modes = physical_device.getSurfacePresentModesKHR(surface);
	LOGI("Surface supports the following present modes:");
	for (auto &present_mode : present_modes)
	{
		LOGI("  \t{}", vk::to_string(present_mode));
	}

	// Chose best properties based on surface capabilities
	properties.image_count    = choose_image_count(image_count, surface_capabilities.minImageCount, surface_capabilities.maxImageCount);
	properties.extent         = choose_extent(extent, surface_capabilities.minImageExtent, surface_capabilities.maxImageExtent, surface_capabilities.currentExtent);
	properties.array_layers   = choose_image_array_layers(1U, surface_capabilities.maxImageArrayLayers);
	properties.surface_format = choose_surface_format(properties.surface_format, surface_formats, surface_format_priority_list);

	vk::FormatProperties format_properties = physical_device.getFormatProperties(properties.surface_format.format);

	this->image_usage_flags    = choose_image_usage(image_usage_flags, surface_capabilities.supportedUsageFlags, format_properties.optimalTilingFeatures);
	properties.image_usage     = composite_image_flags(this->image_usage_flags);
	properties.pre_transform   = choose_transform(transform, surface_capabilities.supportedTransforms, surface_capabilities.currentTransform);
	properties.composite_alpha = choose_composite_alpha(vk::CompositeAlphaFlagBitsKHR::eInherit, surface_capabilities.supportedCompositeAlpha);

	// Pass through defaults to the create function
	properties.old_swapchain = old_swapchain.get_handle();
	properties.present_mode  = present_mode;
}

Swapchain::~Swapchain()
{
	if (operator bool())
	{
		device.get_handle().destroy(*this);
	}
}

Swapchain::Swapchain(Swapchain &&other) :
    vk::SwapchainKHR{other},
    device{other.device},
    surface{other.surface},
    image_usage_flags{std::move(other.image_usage_flags)},
    images{std::move(other.images)},
    properties{std::move(other.properties)}
{
	static_cast<vk::SwapchainKHR &&>(other) = nullptr;
}

void Swapchain::create()
{
	// Revalidate the present mode and surface format
	properties.present_mode   = choose_present_mode(properties.present_mode, present_modes, present_mode_priority_list);
	properties.surface_format = choose_surface_format(properties.surface_format, surface_formats, surface_format_priority_list);

	vk::SwapchainCreateInfoKHR create_info;
	create_info.minImageCount    = properties.image_count;
	create_info.imageExtent      = properties.extent;
	create_info.presentMode      = properties.present_mode;
	create_info.imageFormat      = properties.surface_format.format;
	create_info.imageColorSpace  = properties.surface_format.colorSpace;
	create_info.imageArrayLayers = properties.array_layers;
	create_info.imageUsage       = properties.image_usage;
	create_info.preTransform     = properties.pre_transform;
	create_info.compositeAlpha   = properties.composite_alpha;
	create_info.oldSwapchain     = properties.old_swapchain;
	create_info.surface          = surface;

	static_cast<vk::SwapchainKHR &>(*this) = device.get_handle().createSwapchainKHR(create_info);

	images = device.get_handle().getSwapchainImagesKHR(*this);
}

bool Swapchain::is_valid() const
{
	return operator bool();
}

Device &Swapchain::get_device()
{
	return device;
}

vk::SwapchainKHR Swapchain::get_handle() const
{
	return static_cast<const vk::SwapchainKHR &>(*this);
}

SwapchainProperties &Swapchain::get_properties()
{
	return properties;
}

vk::Result Swapchain::acquire_next_image(uint32_t &image_index, vk::Semaphore image_acquired_semaphore, vk::Fence fence)
{
	auto resultValue = device.get_handle().acquireNextImageKHR(*this, std::numeric_limits<uint64_t>::max(), image_acquired_semaphore, fence);
	image_index      = resultValue.value;
	return resultValue.result;
}

const vk::Extent2D &Swapchain::get_extent() const
{
	return properties.extent;
}

vk::Format Swapchain::get_format() const
{
	return properties.surface_format.format;
}

const std::vector<vk::Image> &Swapchain::get_images() const
{
	return images;
}

vk::SurfaceTransformFlagsKHR Swapchain::get_transform() const
{
	return properties.pre_transform;
}

vk::SurfaceKHR Swapchain::get_surface() const
{
	return surface;
}

vk::ImageUsageFlags Swapchain::get_usage() const
{
	return properties.image_usage;
}

void Swapchain::set_present_mode_priority(const std::vector<vk::PresentModeKHR> &new_present_mode_priority_list)
{
	present_mode_priority_list = new_present_mode_priority_list;
}

void Swapchain::set_surface_format_priority(const std::vector<vk::SurfaceFormatKHR> &new_surface_format_priority_list)
{
	surface_format_priority_list = new_surface_format_priority_list;
}
vk::PresentModeKHR Swapchain::get_present_mode() const
{
	return present_mode;
}
}        // namespace vkb
