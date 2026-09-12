add_library(zenblitz_runtime3d STATIC
    src/runtime3d.cpp
    src/bb_cmds_graphics.cpp
    src/bb_cmds_canvas.cpp
)
target_include_directories(zenblitz_runtime3d PUBLIC include)
target_link_libraries(zenblitz_runtime3d PUBLIC zen_static zenblitz_engine)
target_compile_options(zenblitz_runtime3d PRIVATE $<$<CONFIG:Release>:-O3>)

# zenblitz3d: Marco 1 CLI (compiles and runs a .bb with the window/input
# commands). Reuses cli/backend_stdio.cpp for Print/files, same as zenblitz.
add_executable(zenblitz3d src/main3d.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../cli/backend_stdio.cpp)
target_include_directories(zenblitz3d PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../cli)
target_link_libraries(zenblitz3d PRIVATE zenblitz_runtime3d)
target_compile_options(zenblitz3d PRIVATE $<$<CONFIG:Release>:-O3>)
set_target_properties(zenblitz3d PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
