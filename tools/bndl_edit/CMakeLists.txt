set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Find Qt Library
find_package(Qt5 COMPONENTS Widgets Multimedia REQUIRED)

add_executable(bndl_edit WIN32 main.cpp editor.cpp editor.hpp)

target_link_libraries(bndl_edit libbndl Qt5::Widgets Qt5::Multimedia)
target_include_directories(bndl_edit PRIVATE)

set_property(TARGET bndl_edit PROPERTY CXX_STANDARD 17)

add_custom_command(TARGET bndl_edit POST_BUILD
				   COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libbndl>
																 $<TARGET_FILE:Qt5::Core>
																 $<TARGET_FILE:Qt5::Gui>
																 $<TARGET_FILE:Qt5::Widgets>
																 $<TARGET_FILE:Qt5::Multimedia>
				   $<TARGET_FILE_DIR:bndl_edit>)
