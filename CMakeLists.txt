cmake_minimum_required(VERSION 3.20)
project(ImGuiIdleDemo CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(NOMINMAX)

# --- Dear ImGui (raw source, AI-friendly layout) ---
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

# --- FFmpeg Shared (Gyan.dev release build) ---
set(FFMPEG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg)

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
    ui_mediaplayer.cpp
    ui_titlehead.cpp
    ui_videocards.cpp
    ui_descriptioncard.cpp
    video_finder.cpp
    thumbnail_loader.cpp
    ui_settings.cpp
    worker.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(ImGuiIdleDemo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${FFMPEG_DIR}/include
)

target_link_directories(ImGuiIdleDemo PRIVATE
    ${FFMPEG_DIR}/lib
)

# Win32 + DirectX 11 + Shell + OLE/ADO + FFmpeg shared libraries
target_link_libraries(ImGuiIdleDemo PRIVATE
    d3d11
    d3dcompiler
    dxgi
    dwmapi
    shell32
    ole32
    oleaut32
    avcodec
    avformat
    avutil
    swresample
    swscale
    avfilter
    avdevice
)

# Automatically copy FFmpeg DLLs to output directory for seamless debugging/execution
add_custom_command(TARGET ImGuiIdleDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
        "${FFMPEG_DIR}/bin"
        "$<TARGET_FILE_DIR:ImGuiIdleDemo>"
)

if (MSVC)
    target_compile_options(ImGuiIdleDemo PRIVATE /W3 /EHsc /wd6011)
    # Keep console off (WIN32 app), but allow debug output
    set_target_properties(ImGuiIdleDemo PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ImGuiIdleDemo)
endif()

