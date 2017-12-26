set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Find Qt Library
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Multimedia REQUIRED)

add_executable(bndl_edit WIN32 main.cpp editor.cpp editor.hpp)

target_link_libraries(bndl_edit libbndl Qt5::Widgets Qt5::Multimedia)
target_include_directories(bndl_edit PRIVATE)

set_property(TARGET bndl_edit PROPERTY CXX_STANDARD 14)

add_custom_command( TARGET bndl_edit POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/../files $<TARGET_FILE_DIR:bndl_edit>)
