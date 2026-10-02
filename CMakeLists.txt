cmake_minimum_required(VERSION 3.20)
project(window_shot LANGUAGES CXX)

add_executable(window-shot main.cpp)
target_compile_features(window-shot PRIVATE cxx_std_20)
target_compile_definitions(window-shot PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(window-shot PRIVATE gdiplus dwmapi user32 gdi32)
