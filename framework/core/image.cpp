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

#include "image.h"

#include "device.h"
#include "image_view.h"

namespace vkb
{
namespace
{
inline vk::ImageType find_image_type(vk::Extent3D extent)
{
	vk::ImageType result;

	uint32_t dim_num{0};

	if (extent.width >= 1)
	{
		dim_num++;
	}

	if (extent.height >= 1)
	{
		dim_num++;
	}

	if (extent.depth > 1)
	{
		dim_num++;
	}

	switch (dim_num)
	{
		case 1:
			result = vk::ImageType::e1D;
			break;
		case 2:
			result = vk::ImageType::e2D;
			break;
		case 3:
			result = vk::ImageType::e3D;
			break;
		default:
			throw std::runtime_error("No image type found.");
			break;
	}

	return result;
}
}        // namespace

namespace core
{
Image::Image(Device &                device,
             const vk::Extent3D &    extent,
             vk::Format              format,
             vk::ImageUsageFlags     image_usage,
             vma::MemoryUsage        memory_usage,
             vk::SampleCountFlagBits sample_count,
             const uint32_t          mip_levels,
             const uint32_t          array_layers,
             vk::ImageTiling         tiling,
             vk::ImageCreateFlags    flags) :
    device{device},
    type{find_image_type(extent)},
    extent{extent},
    format{format},
    sample_count{sample_count},
    usage{image_usage},
    array_layer_count{array_layers},
    tiling{tiling}
{
	assert(mip_levels > 0 && "Image should have at least one level");
	assert(array_layers > 0 && "Image should have at least one layer");

	subresource.mipLevel   = mip_levels;
	subresource.arrayLayer = array_layers;
	if (vkb::is_depth_only_format(format))
	{
		subresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
	}
	else if (vkb::is_depth_stencil_format(format))
	{
		subresource.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
	}
	else
	{
		subresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	}

	vk::ImageCreateInfo image_info;
	image_info.flags       = flags;
	image_info.imageType   = type;
	image_info.format      = format;
	image_info.extent      = extent;
	image_info.mipLevels   = mip_levels;
	image_info.arrayLayers = array_layers;
	image_info.samples     = sample_count;
	image_info.tiling      = tiling;
	image_info.usage       = image_usage;

	vma::Allocation::CreateInfo memory_info;
	memory_info.usage = memory_usage;

	if (image_usage & vk::ImageUsageFlagBits::eTransientAttachment)
	{
		memory_info.preferredFlags = vk::MemoryPropertyFlagBits::eLazilyAllocated;
	}

	device.get_memory_allocator().createImage(
	    image_info,
	    memory_info,
	    *this,
	    memory);
}

Image::Image(Device &device, vk::Image handle, const vk::Extent3D &extent, vk::Format format, vk::ImageUsageFlags image_usage) :
    vk::Image{handle},
    device{device},
    type{find_image_type(extent)},
    extent{extent},
    format{format},
    sample_count{vk::SampleCountFlagBits::e1},
    usage{image_usage}
{
	subresource.mipLevel   = 1;
	subresource.arrayLayer = 1;
}

Image::Image(Image &&other) :
    vk::Image{other},
    device{other.device},
    memory{other.memory},
    type{other.type},
    extent{other.extent},
    format{other.format},
    sample_count{other.sample_count},
    usage{other.usage},
    tiling{other.tiling},
    subresource{other.subresource},
    mapped_data{other.mapped_data},
    mapped{other.mapped}
{
	static_cast<vk::Image &>(other) = nullptr;
	other.memory                    = VK_NULL_HANDLE;
	other.mapped_data               = nullptr;
	other.mapped                    = false;

	// Update image views references to this image to avoid dangling pointers
	for (auto &view : views)
	{
		view->set_image(*this);
	}
}

Image::~Image()
{
	if (operator bool() && memory)
	{
		unmap();
		device.get_memory_allocator().destroyImage(*this, memory);
	}
}

Device &Image::get_device()
{
	return device;
}

vk::Image Image::get_handle() const
{
	return static_cast<const vk::Image &>(*this);
}

VmaAllocation Image::get_memory() const
{
	return memory;
}

uint8_t *Image::map()
{
	if (!mapped_data)
	{
		if (tiling != vk::ImageTiling::eLinear)
		{
			LOGW("Mapping image memory that is not linear");
		}
		mapped_data = (uint8_t *) memory.mapMemory();
		mapped      = true;
	}
	return mapped_data;
}

void Image::unmap()
{
	if (mapped)
	{
		memory.unmapMemory();
		mapped_data = nullptr;
		mapped      = false;
	}
}

vk::ImageType Image::get_type() const
{
	return type;
}

const vk::Extent3D &Image::get_extent() const
{
	return extent;
}

vk::Format Image::get_format() const
{
	return format;
}

vk::SampleCountFlagBits Image::get_sample_count() const
{
	return sample_count;
}

vk::ImageUsageFlags Image::get_usage() const
{
	return usage;
}

vk::ImageTiling Image::get_tiling() const
{
	return tiling;
}

const vk::ImageSubresource &Image::get_subresource() const
{
	return subresource;
}

uint32_t Image::get_array_layer_count() const
{
	return array_layer_count;
}

std::unordered_set<ImageView *> &Image::get_views()
{
	return views;
}

}        // namespace core
}        // namespace vkb
