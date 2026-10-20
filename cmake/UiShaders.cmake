file(GLOB UI_FXC_PATHS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
list(SORT UI_FXC_PATHS COMPARE NATURAL ORDER DESCENDING)
find_program(UI_FXC fxc HINTS "$ENV{WindowsSdkVerBinPath}/x64" ${UI_FXC_PATHS} REQUIRED)
set(UI_SHADER_HEADERS)
foreach(STAGE IN ITEMS vs ps)
    set(HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/ui_${STAGE}.h")
    add_custom_command(OUTPUT "${HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND "${UI_FXC}" /nologo /WX /O3 /T ${STAGE}_5_0 /E ${STAGE}_main
            /Vn ui_${STAGE} /Fh "${HEADER}" "${PROJECT_SOURCE_DIR}/src/ui/shaders/ui.hlsl"
        DEPENDS "${PROJECT_SOURCE_DIR}/src/ui/shaders/ui.hlsl"
            "${PROJECT_SOURCE_DIR}/src/ui/shaders/icons.hlsl" VERBATIM)
    list(APPEND UI_SHADER_HEADERS "${HEADER}")
endforeach()
add_library(spotic_renderer STATIC src/ui/renderer.c src/ui/renderer_device.c
    src/ui/renderer_batches.c src/ui/renderer_clay.c src/ui/icons.c ${UI_SHADER_HEADERS})
target_include_directories(spotic_renderer PUBLIC src vendor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(spotic_renderer PUBLIC spotic_build_options PRIVATE d3d11 dxgi dxguid user32)
