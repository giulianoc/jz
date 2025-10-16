set(LIB_SOURCES src/jz_parser.cpp )
set(LIB_HEADERS src/jz_parser.hpp)

include_directories("${NLOHMANN_INCLUDE_DIR}")

add_library(jz SHARED ${LIB_SOURCES} ${LIB_HEADERS})

install (TARGETS jz DESTINATION services/cms-getter)

#####################

set(TEST_SOURCES test/test_jz_parser.cpp)

include_directories("./src")
include_directories("${CATCH2_INCLUDE_DIR}/src")
include_directories("${CATCH2_INCLUDE_DIR}/build/generated-includes")
include_directories("${NLOHMANN_INCLUDE_DIR}")

link_directories("./src")
link_directories("${CATCH2_LIB_DIR}")

add_executable(test_jz_parser ${TEST_SOURCES})

target_link_libraries(test_jz_parser jz)
target_link_libraries(test_jz_parser Catch2)
target_link_libraries(test_jz_parser Catch2Main)

