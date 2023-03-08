
if(NOT model)
    message(FATAL_ERROR "model undefined")
endif()

if(NOT exconfig)
    message(FATAL_ERROR "exconfig undefined")
endif ()

set(suffix ${model}-${exconfig})

add_executable(eapp-${suffix} ${CMAKE_CURRENT_SOURCE_DIR}/vta.cc ${CMAKE_CURRENT_SOURCE_DIR}/../../util/util.c)
vta_default_dirs(eapp-${suffix} ${exconfig} 0)
target_link_libraries(eapp-${suffix} "-static" vta-shared lzma memory graph_executor common vta ${model}  ${model}-ops ${KEYSTONE_LIB_EAPP} m util)
set_target_properties(eapp-${suffix} PROPERTIES OUTPUT_NAME eapp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/eapp
        COMMAND true
        DEPENDS eapp-${suffix})

add_custom_target(vta-direct-linux-${suffix}.ke DEPENDS eapp-${suffix} COMMAND cp eapp vta-direct-linux-${suffix}.ke)
add_dependencies(examples vta-direct-linux-${suffix}.ke)

add_vta_example(direct ${suffix} ${exconfig} 0 eapp)