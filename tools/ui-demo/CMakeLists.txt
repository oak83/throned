cmake_minimum_required(VERSION 3.20)

project(ThronedUiDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)

qt_add_executable(ThronedUiDemo WIN32 MANUAL_FINALIZATION
    main.cpp
)

target_link_libraries(ThronedUiDemo PRIVATE Qt6::Widgets Qt6::Svg)

# The preview shares the production design tokens so its screenshots stay honest.
target_include_directories(ThronedUiDemo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../..")

set_target_properties(ThronedUiDemo PROPERTIES
    OUTPUT_NAME "Throned UI Preview"
)

qt_finalize_executable(ThronedUiDemo)
