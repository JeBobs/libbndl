set(HEADER_DIR ${LIBBNDL_ROOT}/include/libbndl)
set(PUBLIC_HEADERS ${HEADER_DIR}/bundle.hpp)

file(GLOB_RECURSE SRC_FILES
	"*.c"
	"*.h"
    "*.hpp"
    "*.cpp"
)

add_library(libbndl STATIC  ${SRC_FILES}
                            ${PUBLIC_HEADERS})

add_subdirectory(${LIBBNDL_ROOT}/deps/libbinaryio ${CMAKE_CURRENT_BINARY_DIR}/libbinaryio_build)
add_subdirectory(${LIBBNDL_ROOT}/deps/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib_build)
add_subdirectory(${LIBBNDL_ROOT}/deps/pugixml ${CMAKE_CURRENT_BINARY_DIR}/pugixml_build)
add_dependencies(libbndl zlibstatic)
target_link_libraries(libbndl libbinaryio zlibstatic pugixml)
target_include_directories(libbndl PRIVATE ${LIBBNDL_ROOT}/deps/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib_build)

set_property(TARGET libbndl PROPERTY CXX_STANDARD 17)
set_property(TARGET libbndl PROPERTY PREFIX "")
target_include_directories(libbndl PUBLIC ${LIBBNDL_ROOT}/include)
