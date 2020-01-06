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

#include "image_view.h"

#include "core/image.h"
#include "device.h"

namespace vkb
{
namespace core
{
ImageView::ImageView(Image &img, vk::ImageViewType view_type, vk::Format format) :
    device{img.get_device()},
    image{&img},
    format{format}
{
	if (format == vk::Format::eUndefined)
	{
		const_cast<vk::Format&>(this->format) = format = image->get_format();
	}

	subresource_range.levelCount = image->get_subresource().mipLevel;
	subresource_range.layerCount = image->get_subresource().arrayLayer;

	if (is_depth_only_format(format))
	{
		subresource_range.aspectMask = vk::ImageAspectFlagBits::eDepth;
	}
	else if (is_depth_stencil_format(format))
	{
		subresource_range.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
	}
	else
	{
		subresource_range.aspectMask = vk::ImageAspectFlagBits::eColor;
	}

	vk::ImageViewCreateInfo view_info;
	view_info.image            = image->get_handle();
	view_info.viewType         = view_type;
	view_info.format           = format;
	view_info.subresourceRange = subresource_range;

	static_cast<vk::ImageView &>(*this) = device.get_handle().createImageView(view_info);

	// Register this image view to its image
	// in order to be notified when it gets moved
	image->get_views().emplace(this);
}

ImageView::ImageView(ImageView &&other) :
    vk::ImageView{other},
    device{other.device},
    image{other.image},
    format{other.format},
    subresource_range{other.subresource_range}
{
	// Remove old view from image set and add this new one
	auto &views = image->get_views();
	views.erase(&other);
	views.emplace(this);

	static_cast<vk::ImageView &>(other) = nullptr;
}

ImageView::~ImageView()
{
	device.get_handle().destroy(*this);
}

const Image &ImageView::get_image() const
{
	assert(image && "Image view is referring an invalid image");
	return *image;
}

void ImageView::set_image(Image &img)
{
	image = &img;
}

vk::ImageView ImageView::get_handle() const
{
	return static_cast<const vk::ImageView &>(*this);
}

vk::Format ImageView::get_format() const
{
	return format;
}

vk::ImageSubresourceRange ImageView::get_subresource_range() const
{
	return subresource_range;
}

vk::ImageSubresourceLayers ImageView::get_subresource_layers() const
{
	vk::ImageSubresourceLayers subresource;
	subresource.aspectMask     = subresource_range.aspectMask;
	subresource.baseArrayLayer = subresource_range.baseArrayLayer;
	subresource.layerCount     = subresource_range.layerCount;
	subresource.mipLevel       = subresource_range.baseMipLevel;
	return subresource;
}
}        // namespace core
}        // namespace vkb
