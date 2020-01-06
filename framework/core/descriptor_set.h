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

namespace vkb
{
class Device;
class DescriptorSetLayout;
class DescriptorPool;

/**
 * @brief A descriptor set handle allocated from a \ref DescriptorPool.
 *        Destroying the handle has no effect, as the pool manages the lifecycle of its descriptor sets.
 */
class DescriptorSet : protected vk::DescriptorSet
{
  public:
	DescriptorSet(Device &                                    device,
	              DescriptorSetLayout &                       descriptor_set_layout,
	              DescriptorPool &                            descriptor_pool,
	              const BindingMap<vk::DescriptorBufferInfo> &buffer_infos = {},
	              const BindingMap<vk::DescriptorImageInfo> & image_infos  = {});

	DescriptorSet(const DescriptorSet &) = delete;

	DescriptorSet(DescriptorSet &&other);

	// The descriptor set handle will be destroyed when the pool is reset
	~DescriptorSet() = default;

	DescriptorSet &operator=(const DescriptorSet &) = delete;

	DescriptorSet &operator=(DescriptorSet &&) = delete;

	void update(const BindingMap<vk::DescriptorBufferInfo> &buffer_infos,
	            const BindingMap<vk::DescriptorImageInfo> & image_infos);

	const DescriptorSetLayout &get_layout() const;

	vk::DescriptorSet get_handle() const;

	BindingMap<vk::DescriptorBufferInfo> &get_buffer_infos();

	BindingMap<vk::DescriptorImageInfo> &get_image_infos();

  private:
	Device &device;

	DescriptorSetLayout &descriptor_set_layout;

	DescriptorPool &descriptor_pool;

	BindingMap<vk::DescriptorBufferInfo> buffer_infos;

	BindingMap<vk::DescriptorImageInfo> image_infos;
};
}        // namespace vkb
