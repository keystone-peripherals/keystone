if(NOT model)
    message(FATAL_ERROR "model undefined")
endif()

if(NOT exconfig)
    message(FATAL_ERROR "exconfig undefined")
endif ()

set(suffix ${model}_${exconfig})

set(src_dir ${CMAKE_CURRENT_SOURCE_DIR})
set(bin_dir ${CMAKE_CURRENT_BINARY_DIR})

############
## Driver ##
############

add_executable(export-driver-${suffix} ${src_dir}/driver.cc)
target_link_libraries(export-driver-${suffix} "-static" ${KEYSTONE_LIB_EAPP})
set_target_properties(export-driver-${suffix} PROPERTIES OUTPUT_NAME export-driver)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/export-driver
        COMMAND true
        DEPENDS export-driver-${suffix})

####################
## Business logic ##
####################

add_exportable_function(export_func_${suffix} 0x193000 ${src_dir}/export_func.cc ${vta_src_dir}/util/util.c)
vta_default_dirs(export_func_${suffix}_sym ${exconfig} 0)
target_link_libraries(export_func_${suffix}_sym vta-shared vta ${KEYSTONE_LIB_EAPP} pthread memory graph_executor common ${model} ${model}-ops)

add_executable(export-logic-${suffix} ${src_dir}/logic.cc ${CMAKE_CURRENT_SOURCE_DIR}/../../util/util.c)
target_link_libraries(export-logic-${suffix} "-static" vta-shared lzma ${KEYSTONE_LIB_EAPP} pthread export_func_${suffix} ${imgs_src} ${model} ${model}-ops)
vta_default_dirs(export-logic-${suffix} ${exconfig} 0)
set_target_properties(export-logic-${suffix} PROPERTIES OUTPUT_NAME export-logic)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/export-logic
        COMMAND true
        DEPENDS export-logic-${suffix})

##############
## Finalize ##
##############
add_vta_example(export ${suffix} ${exconfig} 0 export-driver export-logic)