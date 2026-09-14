cmake_minimum_required(VERSION 3.21)
project(XRR_Layer_Studio VERSION 2.5.1 LANGUAGES CXX RC)
find_package(Threads REQUIRED)

add_executable(XRR_Layer_Studio WIN32
    src/app_win.cpp
    src/bruker_reader.cpp
    src/fft.cpp
    src/fft_dialog_win.cpp
    src/material_presets.cpp
    src/material_picker_win.cpp
    src/project.cpp
    src/simulation.cpp
    src/fit.cpp
    src/fit_dialog_win.cpp
    src/plot_win.cpp
    app.rc
)

target_compile_features(XRR_Layer_Studio PRIVATE cxx_std_17)
target_compile_definitions(XRR_Layer_Studio PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(XRR_Layer_Studio PRIVATE
    Threads::Threads
    comctl32
    comdlg32
    shell32
    gdiplus
    gdi32
    uxtheme
)

if(MSVC)
    target_compile_options(XRR_Layer_Studio PRIVATE /W4 /permissive- /utf-8 /EHsc)
    set_property(TARGET XRR_Layer_Studio PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(XRR_Layer_Studio PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(XRR_Layer_Studio PROPERTIES
    OUTPUT_NAME "XRR_Layer_Studio_CPP_v2.5.1"
)
