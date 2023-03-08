if(NOT model)
    message(FATAL_ERROR "model undefined")
endif()

if(NOT exconfig)
    message(FATAL_ERROR "exconfig undefined")
endif ()

set(suffix ${model}-${exconfig})

set(src_dir ${CMAKE_CURRENT_SOURCE_DIR})
set(bin_dir ${CMAKE_CURRENT_BINARY_DIR})
set(gen_dir ${bin_dir}/gen)

##########
## Eapp ##
##########

# First, create the operator library
add_executable(linux-operator-lib-${suffix} ${vta_src_dir}/eapp/shared/operator-lib.c)
target_compile_options(linux-operator-lib-${suffix} PRIVATE -ffunction-sections -fdata-sections -mcmodel=medany)
vta_default_dirs(linux-operator-lib-${suffix} ${exconfig} 0)
target_link_libraries(linux-operator-lib-${suffix} ${model}-ops m gcc)

# Generate the linkscript for the operator library
add_library(linux-operator-linkscript-${suffix} OBJECT ${vta_src_dir}/eapp/shared/operator-lib.ld.S)
target_compile_options(linux-operator-linkscript-${suffix} PRIVATE -P -E -x c)
target_compile_definitions(linux-operator-linkscript-${suffix} PRIVATE __PREPROCESSING__)
vta_default_dirs(linux-operator-linkscript-${suffix} ${exconfig} 0)
set_target_properties(linux-operator-linkscript-${suffix} PROPERTIES PREFIX "")
set_target_properties(linux-operator-linkscript-${suffix} PROPERTIES OUTPUT_NAME operator-lib.ld)
set_target_properties(linux-operator-linkscript-${suffix} PROPERTIES SUFFIX "")

# Link with specific options
target_link_options(linux-operator-lib-${suffix} PRIVATE --static -nostartfiles -nostdlib -e initialize_comms
        -Wl,--no-relax -T $<TARGET_OBJECTS:linux-operator-linkscript-${suffix}>)
add_dependencies(linux-operator-lib-${suffix} linux-operator-linkscript-${suffix})

# Create binary and padded source file
add_custom_command(OUTPUT ${bin_dir}/linux-operator-lib-bin-${suffix}
        DEPENDS ${bin_dir}/linux-operator-lib-${suffix}
        COMMAND ${CMAKE_STRIP} -O binary -o ${bin_dir}/linux-operator-lib-bin-${suffix} ${bin_dir}/linux-operator-lib-${suffix})
hexify(${bin_dir}/linux-operator-lib-bin-${suffix} ${bin_dir}/linux-operator-lib-${suffix}.c operator_lib)

# Create the sender

add_executable(vta-linux-owned-eapp-${suffix} ${src_dir}/linux_owned.cc ${vta_src_dir}/eapp/shared/images.cc ${vta_src_dir}/util/util.c ${bin_dir}/linux-operator-lib-${suffix}.c)
target_link_libraries(vta-linux-owned-eapp-${suffix} "-static" lzma ${model} ${model}-ops ${KEYSTONE_LIB_EAPP} pthread m util)
vta_default_dirs(vta-linux-owned-eapp-${suffix} ${exconfig} 0)
set_target_properties(vta-linux-owned-eapp-${suffix} PROPERTIES OUTPUT_NAME vta-linux-owned-eapp)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vta-linux-owned-eapp
        COMMAND true
        DEPENDS vta-linux-owned-eapp-${suffix})

########################
## Host and Packaging ##
########################

add_executable(vta-linux-owned-runner-${suffix} ${vta_src_dir}/host/host_vta.cpp ${vta_src_dir}/eapp/shared/platform.cc ${vta_src_dir}/util/util.c)
target_link_libraries(vta-linux-owned-runner-${suffix} "-static" ${KEYSTONE_LIB_HOST} ${KEYSTONE_LIB_EDGE}  vta ${KEYSTONE_LIB_EAPP} memory graph_executor common)
vta_default_dirs(vta-linux-owned-runner-${suffix} ${exconfig} 0)
set_target_properties(vta-linux-owned-runner-${suffix} PROPERTIES OUTPUT_NAME vta-linux-owned-runner)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vta-linux-owned-runner
        COMMAND true
        DEPENDS vta-linux-owned-runner-${suffix})

set(eyrie_files_to_copy .options_log eyrie-rt)
add_eyrie_runtime(vta-linux-owned-eyrie-${suffix} ${eyrie_plugins} ${eyrie_files_to_copy})

add_custom_target(vta-linux-owned-runner-script-${suffix}
        BYPRODUCTS ${gen_dir}/run-linux-owned.sh
        DEPENDS ${vta_src_dir}/scripts/run-linux-owned.sh
        COMMAND mkdir -p ${gen_dir}
        COMMAND cp ${vta_src_dir}/scripts/run-linux-owned.sh ${gen_dir}/run-linux-owned.sh)

add_keystone_package(vta-linux-owned-package-${suffix}
    "vta-linux-owned-${suffix}.ke"
    "./run-linux-owned.sh"
    ${eyrie_files_to_copy} vta-linux-owned-eapp vta-linux-owned-runner ${gen_dir}/run-linux-owned.sh)
add_dependencies(vta-linux-owned-package-${suffix} vta-linux-owned-eyrie-${suffix})

# add package to the top-level target
add_dependencies(examples vta-linux-owned-package-${suffix})
