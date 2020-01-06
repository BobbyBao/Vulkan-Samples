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

#include "semaphore_pool.h"

#include "core/device.h"

namespace vkb
{
SemaphorePool::SemaphorePool(Device &device) :
    device{device}
{
}

SemaphorePool::~SemaphorePool()
{
	reset();

	// Destroy all semaphores
	for (vk::Semaphore semaphore : semaphores)
	{
		device.get_handle().destroy(semaphore);
	}

	semaphores.clear();
}

vk::Semaphore SemaphorePool::request_semaphore()
{
	// Check if there is an available semaphore
	if (active_semaphore_count < semaphores.size())
	{
		return semaphores.at(active_semaphore_count++);
	}

	vk::Semaphore semaphore = device.get_handle().createSemaphore({});

	semaphores.push_back(semaphore);

	active_semaphore_count++;

	return semaphore;
}

void SemaphorePool::reset()
{
	active_semaphore_count = 0;
}

uint32_t SemaphorePool::get_active_semaphore_count() const
{
	return active_semaphore_count;
}
}        // namespace vkb
