// Copyright (C) 2023 Jérôme "Lynix" Leclercq (lynix680@gmail.com)
// This file is part of the "Nazara Engine - Vulkan renderer"
// For conditions of distribution and use, see copyright notice in Config.hpp

#pragma once

#ifndef NAZARA_VULKANRENDERER_WRAPPER_FRAMEBUFFER_HPP
#define NAZARA_VULKANRENDERER_WRAPPER_FRAMEBUFFER_HPP

#include <NazaraUtils/Prerequisites.hpp>
#include <Nazara/VulkanRenderer/Wrapper/DeviceObject.hpp>

namespace Nz 
{
	namespace Vk
	{
		class Framebuffer : public DeviceObject<Framebuffer, VkFramebuffer, VkFramebufferCreateInfo, VK_OBJECT_TYPE_FRAMEBUFFER>
		{
			friend DeviceObject;

			public:
				Framebuffer() = default;
				Framebuffer(const Framebuffer&) = delete;
				Framebuffer(Framebuffer&&) = default;
				~Framebuffer() = default;

				Framebuffer& operator=(const Framebuffer&) = delete;
				Framebuffer& operator=(Framebuffer&&) = delete;

			private:
				static inline VkResult CreateHelper(Device& device, const VkFramebufferCreateInfo* createInfo, const VkAllocationCallbacks* allocator, VkFramebuffer* handle);
				static inline void DestroyHelper(Device& device, VkFramebuffer handle, const VkAllocationCallbacks* allocator);
		};
	}
}

#include <Nazara/VulkanRenderer/Wrapper/Framebuffer.inl>

#endif // NAZARA_VULKANRENDERER_WRAPPER_FRAMEBUFFER_HPP
