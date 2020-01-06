/* Copyright (c) 2018-2019, Arm Limited and Contributors
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

#include <mutex>

#include "common/error.h"

VKBP_DISABLE_WARNINGS()
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize.h>
VKBP_ENABLE_WARNINGS()

#include "common/utils.h"
#include "platform/filesystem.h"
#include "scene_graph/components/image/astc.h"
#include "scene_graph/components/image/ktx.h"
#include "scene_graph/components/image/stb.h"

namespace vkb
{
namespace sg
{
bool is_astc(const vk::Format format)
{
	return (format == vk::Format::eAstc4x4UnormBlock ||
	        format == vk::Format::eAstc4x4SrgbBlock ||
	        format == vk::Format::eAstc5x4UnormBlock ||
	        format == vk::Format::eAstc5x4SrgbBlock ||
	        format == vk::Format::eAstc5x5UnormBlock ||
	        format == vk::Format::eAstc5x5SrgbBlock ||
	        format == vk::Format::eAstc6x5UnormBlock ||
	        format == vk::Format::eAstc6x5SrgbBlock ||
	        format == vk::Format::eAstc6x6UnormBlock ||
	        format == vk::Format::eAstc6x6SrgbBlock ||
	        format == vk::Format::eAstc8x5UnormBlock ||
	        format == vk::Format::eAstc8x5SrgbBlock ||
	        format == vk::Format::eAstc8x6UnormBlock ||
	        format == vk::Format::eAstc8x6SrgbBlock ||
	        format == vk::Format::eAstc8x8UnormBlock ||
	        format == vk::Format::eAstc8x8SrgbBlock ||
	        format == vk::Format::eAstc10x5UnormBlock ||
	        format == vk::Format::eAstc10x5SrgbBlock ||
	        format == vk::Format::eAstc10x6UnormBlock ||
	        format == vk::Format::eAstc10x6SrgbBlock ||
	        format == vk::Format::eAstc10x8UnormBlock ||
	        format == vk::Format::eAstc10x8SrgbBlock ||
	        format == vk::Format::eAstc10x10UnormBlock ||
	        format == vk::Format::eAstc10x10SrgbBlock ||
	        format == vk::Format::eAstc12x10UnormBlock ||
	        format == vk::Format::eAstc12x10SrgbBlock ||
	        format == vk::Format::eAstc12x12UnormBlock ||
	        format == vk::Format::eAstc12x12SrgbBlock);
}

Image::Image(const std::string &name, std::vector<uint8_t> &&d, std::vector<Mipmap> &&m) :
    Component{name},
    data{std::move(d)},
    format{vk::Format::eR8G8B8A8Unorm},
    mipmaps{std::move(m)}
{
}

std::type_index Image::get_type()
{
	return typeid(Image);
}

const std::vector<uint8_t> &Image::get_data() const
{
	return data;
}

void Image::clear_data()
{
	data.clear();
	data.shrink_to_fit();
}

vk::Format Image::get_format() const
{
	return format;
}

const vk::Extent3D &Image::get_extent() const
{
	return mipmaps.at(0).extent;
}

const uint32_t Image::get_layers() const
{
	return layers;
}

const std::vector<Mipmap> &Image::get_mipmaps() const
{
	return mipmaps;
}

std::vector<vk::BufferImageCopy> Image::get_buffer_copy_regions() const
{
	std::vector<vk::BufferImageCopy> buffer_copy_regions;
	for (uint32_t layer = 0; layer < layers; layer++)
	{
		for (size_t i = 0; i < mipmaps.size(); i++)
		{
			vk::BufferImageCopy buffer_copy_region;
			buffer_copy_region.imageSubresource.aspectMask     = vk::ImageAspectFlagBits::eColor;
			buffer_copy_region.imageSubresource.mipLevel       = vkb::to_u32(i);
			buffer_copy_region.imageSubresource.baseArrayLayer = layer;
			buffer_copy_region.imageSubresource.layerCount     = 1;
			buffer_copy_region.imageExtent                     = mipmaps.at(i).extent;
			buffer_copy_region.bufferOffset                    = offsets[layer][i];
			buffer_copy_regions.push_back(buffer_copy_region);
		}
	}
	return buffer_copy_regions;
}

const std::vector<std::vector<vk::DeviceSize>> &Image::get_offsets() const
{
	return offsets;
}

void Image::create_vk_image(Device &device, vk::ImageViewType image_view_type, vk::ImageCreateFlags flags)
{
	assert(!vk_image && !vk_image_view && "Vulkan image already constructed");

	vk_image = std::make_unique<core::Image>(device,
	                                         get_extent(),
	                                         format,
	                                         vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                                         vma::MemoryUsage::eGpuOnly,
	                                         vk::SampleCountFlagBits::e1,
	                                         to_u32(mipmaps.size()),
	                                         layers,
	                                         vk::ImageTiling::eOptimal,
	                                         flags);

	vk_image_view = std::make_unique<core::ImageView>(*vk_image, image_view_type);
}

const core::Image &Image::get_vk_image() const
{
	assert(vk_image && "Vulkan image was not created");
	return *vk_image;
}

const core::ImageView &Image::get_vk_image_view() const
{
	assert(vk_image_view && "Vulkan image view was not created");
	return *vk_image_view;
}

Mipmap &Image::get_mipmap(const size_t index)
{
	return mipmaps.at(index);
}

void Image::generate_mipmaps()
{
	assert(mipmaps.size() == 1 && "Mipmaps already generated");

	if (mipmaps.size() > 1)
	{
		return;        // Do not generate again
	}

	auto extent      = get_extent();
	auto next_width  = std::max<uint32_t>(1u, extent.width / 2);
	auto next_height = std::max<uint32_t>(1u, extent.height / 2);
	auto channels    = 4;
	auto next_size   = next_width * next_height * channels;

	while (true)
	{
		// Make space for next mipmap
		auto old_size = to_u32(data.size());
		data.resize(old_size + next_size);

		auto &prev_mipmap = mipmaps.back();
		// Update mipmaps
		Mipmap next_mipmap{};
		next_mipmap.level  = prev_mipmap.level + 1;
		next_mipmap.offset = old_size;
		next_mipmap.extent = {next_width, next_height, 1u};

		// Fill next mipmap memory
		stbir_resize_uint8(data.data() + prev_mipmap.offset, prev_mipmap.extent.width, prev_mipmap.extent.height, 0,
		                   data.data() + next_mipmap.offset, next_mipmap.extent.width, next_mipmap.extent.height, 0, channels);

		mipmaps.emplace_back(std::move(next_mipmap));

		// Next mipmap values
		next_width  = std::max<uint32_t>(1u, next_width / 2);
		next_height = std::max<uint32_t>(1u, next_height / 2);
		next_size   = next_width * next_height * channels;

		if (next_width == 1 && next_height == 1)
		{
			break;
		}
	}
}

std::vector<Mipmap> &Image::get_mut_mipmaps()
{
	return mipmaps;
}

std::vector<uint8_t> &Image::get_mut_data()
{
	return data;
}

void Image::set_data(const uint8_t *raw_data, size_t size)
{
	assert(data.empty() && "Image data already set");
	data = {raw_data, raw_data + size};
}

void Image::set_format(const vk::Format f)
{
	format = f;
}

void Image::set_width(const uint32_t width)
{
	mipmaps.at(0).extent.width = width;
}

void Image::set_height(const uint32_t height)
{
	mipmaps.at(0).extent.height = height;
}

void Image::set_depth(const uint32_t depth)
{
	mipmaps.at(0).extent.depth = depth;
}

void Image::set_layers(uint32_t l)
{
	layers = l;
}

void Image::set_offsets(const std::vector<std::vector<vk::DeviceSize>> &o)
{
	offsets = o;
}

std::unique_ptr<Image> Image::load(const std::string &name, const std::string &uri)
{
	std::unique_ptr<Image> image{nullptr};

	auto data = fs::read_asset(uri);

	// Get extension
	auto extension = get_extension(uri);

	if (extension == "png" || extension == "jpg")
	{
		image = std::make_unique<Stb>(name, data);
	}
	else if (extension == "astc")
	{
		image = std::make_unique<Astc>(name, data);
	}
	else if (extension == "ktx")
	{
		image = std::make_unique<Ktx>(name, data);
	}

	return image;
}

}        // namespace sg
}        // namespace vkb
