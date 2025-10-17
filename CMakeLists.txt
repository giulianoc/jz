set(LIB_SOURCES src/jz_parser.cpp )
set(LIB_HEADERS src/jz_parser.hpp)

add_library(jz SHARED ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(jz PRIVATE "${NLOHMANN_INCLUDE_DIR}")

install (TARGETS jz DESTINATION services/cms-getter)


# --- Test ---

set(TEST_SOURCES test/test_jz_parser.cpp)

add_executable(test_jz_parser ${TEST_SOURCES})

target_include_directories(test_jz_parser
	PRIVATE
		./src
		"${NLOHMANN_INCLUDE_DIR}"
		"${CATCH2_INCLUDE_DIR}/src"
		"${CATCH2_INCLUDE_DIR}/build/generated-includes"
)

target_link_directories(test_jz_parser
	PRIVATE
		"${CATCH2_LIB_DIR}"
)

target_link_libraries(test_jz_parser
	PRIVATE
		jz
		Catch2
		Catch2Main
)




