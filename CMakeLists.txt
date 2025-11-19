set(LIB_SOURCES src/JZParser.cpp src/ToolsManager.cpp src/tools/CollectionTools.cpp src/tools/DateTools.cpp src/tools/TemplateTools.cpp src/tools/StringTools.cpp)
set(LIB_HEADERS src/JZParser.hpp src/ToolsManager.hpp src/tools/CollectionTools.hpp src/tools/DateTools.hpp src/tools/TemplateTools.hpp src/tools/StringTools.hpp)

add_library(jz SHARED ${LIB_SOURCES} ${LIB_HEADERS})

target_include_directories(jz PRIVATE "${NLOHMANN_INCLUDE_DIR}")

install(TARGETS jz DESTINATION services/cms-getter)

