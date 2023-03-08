if(NOT model)
    message(FATAL_ERROR "model undefined")
endif()

if(NOT exconfig)
    message(FATAL_ERROR "exconfig undefined")
endif ()

if(NOT msgpassconfig)
    message(FATAL_ERROR "msgpassconfig undefined")
endif ()

set(suffix ${model}-${exconfig}-${msgpassconfig})


set(src_dir ${CMAKE_CURRENT_SOURCE_DIR})
set(bin_dir ${CMAKE_CURRENT_BINARY_DIR})

############
## Sender ##
############

# First, create the operator library
add_executable(operator-lib-${suffix} ${vta_src_dir}/eapp/shared/operator-lib.c)
target_compile_options(operator-lib-${suffix} PRIVATE -ffunction-sections -fdata-sections -mcmodel=medany)
vta_default_dirs(operator-lib-${suffix} ${exconfig} ${msgpassconfig})
target_link_libraries(operator-lib-${suffix} ${model}-ops m gcc)

# Generate the linkscript for the operator library
add_library(operator-linkscript-${suffix} OBJECT ${vta_src_dir}/eapp/shared/operator-lib.ld.S)
target_compile_options(operator-linkscript-${suffix} PRIVATE -P -E -x c)
target_compile_definitions(operator-linkscript-${suffix} PRIVATE __PREPROCESSING__)
vta_default_dirs(operator-linkscript-${suffix} ${exconfig} ${msgpassconfig})
set_target_properties(operator-linkscript-${suffix} PROPERTIES PREFIX "")
set_target_properties(operator-linkscript-${suffix} PROPERTIES OUTPUT_NAME operator-lib.ld)
set_target_properties(operator-linkscript-${suffix} PROPERTIES SUFFIX "")

# Link with specific options
target_link_options(operator-lib-${suffix} PRIVATE --static -nostartfiles -nostdlib -e initialize_comms
        -Wl,--no-relax -T $<TARGET_OBJECTS:operator-linkscript-${suffix}>)
add_dependencies(operator-lib-${suffix} operator-linkscript-${suffix})

# Create binary and padded source file
add_custom_command(OUTPUT ${bin_dir}/operator-lib-bin-${suffix}
        DEPENDS ${bin_dir}/operator-lib-${suffix}
        COMMAND ${CMAKE_STRIP} -O binary -o ${bin_dir}/operator-lib-bin-${suffix} ${bin_dir}/operator-lib-${suffix})
pad(${bin_dir}/operator-lib-bin-${suffix} ${bin_dir}/operator-lib-pad-${suffix} 0x1000)
hexify_aligned(${bin_dir}/operator-lib-pad-${suffix} ${bin_dir}/operator-lib-${suffix}.c operator_lib 0x1000)

# Create the sender
add_executable(mp-sender-${suffix} ${src_dir}/sender.c ${bin_dir}/operator-lib-${suffix}.c ${src_dir}/../../util/util.c )
target_link_libraries(mp-sender-${suffix} "-static" vta-shared ${model} lzma ${KEYSTONE_LIB_EAPP} pthread m util)
vta_default_dirs(mp-sender-${suffix} ${exconfig} ${msgpassconfig})
set_target_properties(mp-sender-${suffix} PROPERTIES OUTPUT_NAME mp-sender)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mp-sender
        COMMAND true
        DEPENDS mp-sender-${suffix})

##############
## Receiver ##
##############

add_executable(mp-receiver-${suffix} ${src_dir}/receiver.cc ${vta_src_dir}/util/util.c)
vta_default_dirs(mp-receiver-${suffix} ${exconfig} ${msgpassconfig})
target_link_libraries(mp-receiver-${suffix} "-static" vta-shared vta ${KEYSTONE_LIB_EAPP} pthread memory graph_executor common )
set_target_properties(mp-receiver-${suffix} PROPERTIES OUTPUT_NAME mp-receiver)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mp-receiver
        COMMAND true
        DEPENDS mp-receiver-${suffix})

##############
## Finalize ##
##############
add_vta_example(msg-pass ${suffix} ${exconfig} 0 mp-receiver mp-sender)
