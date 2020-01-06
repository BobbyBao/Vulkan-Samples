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

#include "framebuffer.h"

#include "device.h"

namespace vkb
{
const vk::Framebuffer &Framebuffer::get_handle() const
{
	return static_cast<const vk::Framebuffer &>(*this);
}

const vk::Extent2D &Framebuffer::get_extent() const
{
	return extent;
}

Framebuffer::Framebuffer(Device &device, const RenderTarget &render_target, const RenderPass &render_pass) :
    device{device},
    extent{render_target.get_extent()}
{
	std::vector<vk::ImageView> attachments;

	for (auto &view : render_target.get_views())
	{
		attachments.emplace_back(view.get_handle());
	}

	vk::FramebufferCreateInfo create_info;

	create_info.renderPass      = render_pass.get_handle();
	create_info.attachmentCount = to_u32(attachments.size());
	create_info.pAttachments    = attachments.data();
	create_info.width           = extent.width;
	create_info.height          = extent.height;
	create_info.layers          = 1;

	static_cast<vk::Framebuffer &>(*this) = device.get_handle().createFramebuffer(create_info);
}

Framebuffer::Framebuffer(Framebuffer &&other) :
    vk::Framebuffer{other},
    device{other.device},
    extent{other.extent}
{
	static_cast<vk::Framebuffer &>(*this) = other;
	static_cast<vk::Framebuffer &>(other) = nullptr;
}

Framebuffer::~Framebuffer()
{
	if (*this)
	{
		device.get_handle().destroy(*this);
	}
}
}        // namespace vkb
