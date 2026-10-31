#Load all the dependencies
#vulkan
find_package(Vulkan REQUIRED)

include(FetchContent)

#GLFW
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
)

#imgui
FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.5
        GIT_SHALLOW    TRUE
)

#vk-bootstrap, tag must match vk version
FetchContent_Declare(vk-bootstrap
        GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
        GIT_TAG        v1.4.350
        GIT_SHALLOW    TRUE
)

#VMA
FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
        GIT_TAG        v3.4.0+2
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES_RECURSE TRUE
)

# fastgltf: glTF 2.0 / GLB scene importer, included by assets through fastgltf::fastgltf.
# fastgltf.ixx includes the STL inside its module purview, which clang rejects against the MSVC STL.
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_CPP_MODULES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(fastgltf
        GIT_REPOSITORY https://github.com/spnda/fastgltf.git
        GIT_TAG        v0.9.0
        GIT_SHALLOW    TRUE
)

# GLM. (for GL target)
FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.3
        GIT_SHALLOW    TRUE
)

# stb
FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
)

# bc7enc (BC7 encoder)
FetchContent_Declare(bc7enc
        GIT_REPOSITORY https://github.com/richgel999/bc7enc.git
        GIT_TAG        f66c2e489b07138f2673a2fb3d27c1aa1d565c48
        SOURCE_SUBDIR  encoder-only
)

#Meshoptimizer
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
        GIT_TAG        v1.1
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glfw imgui vk-bootstrap vma fastgltf meshoptimizer stb bc7enc)

# glm 1.0.1 asks for CMake 3.6; CMake 4 deprecates anything below 3.10.
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
FetchContent_MakeAvailable(glm)
unset(CMAKE_POLICY_VERSION_MINIMUM)
