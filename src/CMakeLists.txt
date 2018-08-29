include(GenerateExportHeader)

option(LIBBNDL_BUILD_STATIC "Build libbndl as a static library." OFF)

set(HEADER_DIR ${LIBBNDL_ROOT}/include/libbndl)
set(PUBLIC_HEADERS ${HEADER_DIR}/bundle.hpp)

file(GLOB_RECURSE SRC_FILES
	"*.c"
	"*.h"
    "*.hpp"
    "*.cpp"
)

if(LIBBNDL_BUILD_STATIC)
	add_library(libbndl STATIC ${SRC_FILES}
							   ${PUBLIC_HEADERS})
else()
	add_library(libbndl SHARED ${SRC_FILES}
							   ${PUBLIC_HEADERS})
endif()

add_subdirectory(${LIBBNDL_ROOT}/deps/libbinaryio ${CMAKE_CURRENT_BINARY_DIR}/libbinaryio_build EXCLUDE_FROM_ALL)
add_subdirectory(${LIBBNDL_ROOT}/deps/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib_build EXCLUDE_FROM_ALL)
add_subdirectory(${LIBBNDL_ROOT}/deps/pugixml ${CMAKE_CURRENT_BINARY_DIR}/pugixml_build EXCLUDE_FROM_ALL)

add_dependencies(libbndl zlibstatic)
target_link_libraries(libbndl libbinaryio zlibstatic)

get_target_property(PUGIXML_INCLUDES pugixml INCLUDE_DIRECTORIES)
target_include_directories(libbndl PRIVATE ${LIBBNDL_ROOT}/deps/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib_build ${PUGIXML_INCLUDES})
target_compile_definitions(libbndl PRIVATE PUGIXML_HEADER_ONLY)

set_property(TARGET libbndl PROPERTY CXX_STANDARD 17)
set_property(TARGET libbndl PROPERTY PREFIX "")
set_property(TARGET libbndl PROPERTY CXX_VISIBILITY_PRESET hidden)

if(LIBBNDL_BUILD_STATIC)
	set_property(TARGET libbndl PROPERTY COMPILE_FLAGS -DLIBBNDL_STATIC_DEFINE)
endif()
generate_export_header(libbndl)

target_include_directories(libbndl PUBLIC ${LIBBNDL_ROOT}/include ${CMAKE_CURRENT_BINARY_DIR})

if(MSVC)
	target_compile_options(${PROJECT_NAME} PUBLIC "/permissive-")
endif()

set_property(TARGET libbndl PROPERTY POSITION_INDEPENDENT_CODE ON)

install(TARGETS libbndl RUNTIME DESTINATION bin
						LIBRARY DESTINATION lib
						ARCHIVE DESTINATION lib)
install(FILES ${PUBLIC_HEADERS} ${CMAKE_CURRENT_BINARY_DIR}/libbndl_export.h DESTINATION include/libbndl)
install(FILES ${LIBBNDL_ROOT}/LICENSE.md DESTINATION share/libbndl RENAME copyright)
