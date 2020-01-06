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

#pragma once

#include <unordered_set>

#include "common/helpers.h"
#include "common/vk_common.h"

namespace vkb
{
class Device;

namespace core
{
class ImageView;
class Image : public vk::Image
{
  public:
	Image(Device &            device,
	      vk::Image           handle,
	      const vk::Extent3D &extent,
	      vk::Format          format,
	      vk::ImageUsageFlags image_usage);

	Image(Device &                device,
	      const vk::Extent3D &    extent,
	      vk::Format              format,
	      vk::ImageUsageFlags     image_usage,
	      vma::MemoryUsage        memory_usage,
	      vk::SampleCountFlagBits sample_count = vk::SampleCountFlagBits::e1,
	      uint32_t                mip_levels   = 1,
	      uint32_t                array_layers = 1,
	      vk::ImageTiling         tiling       = vk::ImageTiling::eOptimal,
	      vk::ImageCreateFlags    flags        = {});

	Image(const Image &) = delete;

	Image(Image &&other);

	~Image();

	Image &operator=(const Image &) = delete;

	Image &operator=(Image &&) = delete;

	Device &get_device();

	vk::Image get_handle() const;

	VmaAllocation get_memory() const;

	/**
	 * @brief Maps vulkan memory to an host visible address
	 * @return Pointer to host visible memory
	 */
	uint8_t *map();

	/**
	 * @brief Unmaps vulkan memory from the host visible address
	 */
	void unmap();

	vk::ImageType get_type() const;

	const vk::Extent3D &get_extent() const;

	vk::Format get_format() const;

	vk::SampleCountFlagBits get_sample_count() const;

	vk::ImageUsageFlags get_usage() const;

	vk::ImageTiling get_tiling() const;

	const vk::ImageSubresource& get_subresource() const;

	uint32_t get_array_layer_count() const;

	std::unordered_set<ImageView *> &get_views();

  private:
	Device &device;

	vma::Allocation memory;

	vk::ImageType type;

	vk::Extent3D extent;

	vk::Format format;

	vk::ImageUsageFlags usage;

	vk::SampleCountFlagBits sample_count;

	vk::ImageTiling tiling;

	vk::ImageSubresource subresource;

	uint32_t array_layer_count{0};

	/// Image views referring to this image
	std::unordered_set<ImageView *> views;

	uint8_t *mapped_data{nullptr};

	/// Whether it was mapped
	bool mapped{false};
};
}        // namespace core
}        // namespace vkb
