# VK_Window: GLFW window and its ImGui platform backend.
module(VK_Window
        DEPENDS Logging VulkanHppModule glfw imgui-vk
)
# GLFW includes no GL headers.
target_compile_definitions(VK_Window PUBLIC GLFW_INCLUDE_NONE)
