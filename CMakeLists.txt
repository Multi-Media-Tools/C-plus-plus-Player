cmake_minimum_required(VERSION 3.20)
project(ImGuiIdleDemo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(NOMINMAX)

# --- Dear ImGui (raw source, AI-friendly layout) ---
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(ImGuiIdleDemo WIN32
    main.cpp
    app.cpp
    ui_sidebar.cpp
    ui_homepage.cpp
    ui_settings.cpp
    worker.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(ImGuiIdleDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Win32 + DirectX 11 + rounded dark titlebar (DwmApi)
target_link_libraries(ImGuiIdleDemo PRIVATE
    d3d11
    d3dcompiler
    dxgi
    dwmapi
)

if (MSVC)
    target_compile_options(ImGuiIdleDemo PRIVATE /W3 /EHsc /wd6011)
    # Keep console off (WIN32 app), but allow debug output
    set_target_properties(ImGuiIdleDemo PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ImGuiIdleDemo)
endif()

