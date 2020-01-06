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

#include "common/helpers.h"
#include "common/vk_common.h"
#include "core/image.h"

namespace vkb
{
namespace core
{
class ImageView : protected vk::ImageView
{
  public:
	ImageView(Image &image, vk::ImageViewType view_type, vk::Format format = vk::Format::eUndefined);

	ImageView(ImageView &) = delete;

	ImageView(ImageView &&other);

	~ImageView();

	ImageView &operator=(const ImageView &) = delete;

	ImageView &operator=(ImageView &&) = delete;

	const Image &get_image() const;

	/**
	 * @brief Update the image this view is referring to
	 *        Used on image move
	 */
	void set_image(Image &image);

	vk::ImageView get_handle() const;

	vk::Format get_format() const;

	vk::ImageSubresourceRange get_subresource_range() const;

	vk::ImageSubresourceLayers get_subresource_layers() const;

  private:
	Device &device;

	Image *image{};

	const vk::Format format;

	vk::ImageSubresourceRange subresource_range;
};
}        // namespace core
}        // namespace vkb
