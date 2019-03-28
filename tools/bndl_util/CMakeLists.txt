add_executable(bndl_util main.cpp)

target_link_libraries(bndl_util libbndl)
target_include_directories(bndl_util PRIVATE ${LIBBNDL_ROOT}/deps/cxxopts/include)

set_property(TARGET bndl_util PROPERTY CXX_STANDARD 17)

add_custom_command(TARGET bndl_util POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:libbndl> $<TARGET_FILE_DIR:bndl_util>)
