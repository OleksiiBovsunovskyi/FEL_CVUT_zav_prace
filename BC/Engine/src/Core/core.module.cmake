module(Core
        DEPENDS assets VK_Window
                VulkanHppModule VmaHppModule
                vk-bootstrap::vk-bootstrap
                GPUOpen::VulkanMemoryAllocator
                VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        PRIVATE_DEPENDS "${SLANG_LIBRARY}"
)
