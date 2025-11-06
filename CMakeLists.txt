set(LIB_SOURCES src/JZParser.cpp src/ToolManager.cpp src/tools/ArrayTools.cpp src/tools/DateTools.cpp src/tools/StringTools.cpp)
set(LIB_HEADERS src/JZParser.hpp src/ToolManager.hpp src/tools/ArrayTools.hpp src/tools/DateTools.hpp src/tools/StringTools.hpp)

add_library(jz SHARED ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(jz PRIVATE "${NLOHMANN_INCLUDE_DIR}")

install(TARGETS jz DESTINATION services/cms-getter)


# --- Test ---

set(TEST_SOURCES test/jzParser.cpp)

add_executable(test-jz-parser ${TEST_SOURCES})

target_include_directories(test-jz-parser
        PRIVATE
        ./src
        "${NLOHMANN_INCLUDE_DIR}"
        "${CATCH2_INCLUDE_DIR}"
)

target_link_directories(test-jz-parser
        PRIVATE
        "${CATCH2_LIB_DIR}"
)

target_link_libraries(test-jz-parser
        PRIVATE
        jz
        Catch2Main
        Catch2
)
