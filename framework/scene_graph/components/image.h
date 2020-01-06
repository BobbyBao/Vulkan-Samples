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

#pragma once

#include <memory>
#include <string>
#include <typeinfo>
#include <vector>

#include <volk.h>

#include "core/image.h"
#include "core/image_view.h"
#include "scene_graph/component.h"

namespace vkb
{
namespace sg
{
/**
 * @param format Vulkan format
 * @return Whether the vulkan format is ASTC
 */
bool is_astc(vk::Format format);

/**
 * @brief Mipmap information
 */
struct Mipmap
{
	/// Mipmap level
	uint32_t level = 0;

	/// Byte offset used for uploading
	uint32_t offset = 0;

	/// Width depth and height of the mipmap
	vk::Extent3D extent;
};

class Image : public Component
{
  public:
	Image(const std::string &name, std::vector<uint8_t> &&data = {}, std::vector<Mipmap> &&mipmaps = {{}});

	static std::unique_ptr<Image> load(const std::string &name, const std::string &uri);

	virtual ~Image() = default;

	virtual std::type_index get_type() override;

	const std::vector<uint8_t> &get_data() const;

	void clear_data();

	vk::Format get_format() const;

	const vk::Extent3D &get_extent() const;

	const uint32_t get_layers() const;

	const std::vector<Mipmap> &get_mipmaps() const;

	const std::vector<std::vector<vk::DeviceSize>> &get_offsets() const;

	void generate_mipmaps();

	void create_vk_image(Device &device, vk::ImageViewType image_view_type = vk::ImageViewType::e2D, vk::ImageCreateFlags flags = {});

	const core::Image &get_vk_image() const;

	const core::ImageView &get_vk_image_view() const;

    std::vector<vk::BufferImageCopy> get_buffer_copy_regions() const;

  protected:
	std::vector<uint8_t> &get_mut_data();

	void set_data(const uint8_t *raw_data, size_t size);

	void set_format(vk::Format format);

	void set_width(uint32_t width);

	void set_height(uint32_t height);

	void set_depth(uint32_t depth);

	void set_layers(uint32_t layers);

	void set_offsets(const std::vector<std::vector<vk::DeviceSize>> &offsets);

	Mipmap &get_mipmap(size_t index);

	std::vector<Mipmap> &get_mut_mipmaps();

  private:
	std::vector<uint8_t> data;

	vk::Format format{vk::Format::eUndefined};

	uint32_t layers{1};

	std::vector<Mipmap> mipmaps{{}};

	// Offsets stored like offsets[array_layer][mipmap_layer]
	std::vector<std::vector<vk::DeviceSize>> offsets;

	std::unique_ptr<core::Image> vk_image;

	std::unique_ptr<core::ImageView> vk_image_view;
};

}        // namespace sg
}        // namespace vkb
