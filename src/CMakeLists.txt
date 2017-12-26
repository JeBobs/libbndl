set(HEADER_DIR ${LIBBNDL_ROOT}/include/libbndl)
set(PUBLIC_HEADERS  ${HEADER_DIR}/bundle.hpp)

file(GLOB_RECURSE SRC_FILES
	"*.c"
	"*.h"
    "*.hpp"
    "*.cpp"
)

add_library(libbndl STATIC   ${SRC_FILES}
							${PUBLIC_HEADERS})

set_property(TARGET libbndl PROPERTY CXX_STANDARD 14)
set_property(TARGET libbndl PROPERTY PREFIX "")
target_include_directories(libbndl PUBLIC ${LIBBNDL_ROOT}/include)
