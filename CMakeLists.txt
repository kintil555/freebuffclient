cmake_minimum_required(VERSION 3.20)
project(nova LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(FATAL_ERROR "Nova targets Windows only.")
endif()

# ---------------------------------------------------------------- ImGui -----
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

# GL headers: use the WGL loader shipped with ImGui's GL3 backend.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM=0)

# -------------------------------------------------------------- MinHook -----
set(MH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook)
file(GLOB_RECURSE MH_SRC ${MH_DIR}/src/*.c ${MH_DIR}/src/hde/*.c)
add_library(minhook STATIC ${MH_SRC})
target_include_directories(minhook PUBLIC ${MH_DIR}/include)
# MinHook is C — disable MSVC PCH expectations and set C standard.
set_target_properties(minhook PROPERTIES C_STANDARD 11 LINKER_LANGUAGE C)

# ---------------------------------------------------------------- nova.dll --
add_library(nova SHARED
    src/dllmain.cpp
    src/hook_manager.cpp
    src/modules.cpp
    src/esp.cpp
    src/ui.cpp
)
target_include_directories(nova PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/jni
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/jni/win32
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(nova PRIVATE imgui minhook opengl32 dwmapi user32 gdi32 shell32)
target_compile_definitions(nova PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# Keep code size sane; strip RTTI/exceptions where possible? Keep exceptions ON
# (std::thread + STL use them).
target_compile_options(nova PRIVATE /EHsc /W3)

set_target_properties(nova PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "nova"
)

# ---------------------------------------------------------- nova_injector ---
add_executable(nova_injector
    injector/injector.cpp
)
target_link_libraries(nova_injector PRIVATE dwmapi user32 gdi32 msimg32)
target_compile_definitions(nova_injector PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(nova_injector PRIVATE /EHsc /W3)
set_target_properties(nova_injector PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME "nova_injector"
)
