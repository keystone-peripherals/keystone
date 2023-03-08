
####################
## Configuration ###
####################

set(model resnet18_v2)

if(NOT BUILDROOT_HOST_DIR)
    message(FATAL_ERROR BUILDROOT_HOST_DIR undefined)
endif()

if(NOT BUILDROOT_TARGET_DIR)
    message(FATAL_ERROR BUILDROOT_TARGET_DIR undefined)
endif()

if(RISCV32)
    set(eyrie_plugins "freemem io_syscall net_syscall linux_syscall env_setup drivers callee rv32")
else()
    set(eyrie_plugins "freemem io_syscall net_syscall linux_syscall env_setup drivers callee")
endif()

####################
## Model sources ###
####################

## Various directories ##
set(vta_src_dir     ${CMAKE_CURRENT_SOURCE_DIR})
set(gen_dir         ${CMAKE_CURRENT_BINARY_DIR}/gen)
set(bins_dir        ${gen_dir}/bins/)


## Helper macros ##

macro(pad input output to)
    add_custom_command(OUTPUT ${output} DEPENDS bins-extracted ${input}
        BYPRODUCTS ${output}
        COMMAND mkdir -p ${pad_dir}
        COMMAND cp ${input} ${output}
        COMMAND truncate -s $$\(\(
            ( $$\(stat --printf="%s" ${output} \)
                / ${to} + 1) * ${to}\)\) ${output})
endmacro(pad)

macro(hexify input output name)
    add_custom_command(OUTPUT ${output} DEPENDS bins-extracted ${input}
        BYPRODUCTS ${output}
        COMMAND mkdir -p ${src_dir}
        COMMAND xxd -i -n ${name} ${input} > ${output})
endmacro(hexify)

macro(hexify_aligned input output name to)
    add_custom_command(OUTPUT ${output} DEPENDS bins-extracted ${input}
        BYPRODUCTS ${output}
        COMMAND mkdir -p ${src_dir}
        COMMAND  echo \"__attribute__\(\(aligned\(${to}\)\)\)\" > ${output} &&
            xxd -i -n ${name} ${input} >> ${output})
endmacro(hexify_aligned)

## Binaries ##

add_custom_command(OUTPUT ${gen_dir}/.stamp_extracted
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/bins/bins.tar.xz
        COMMAND mkdir -p ${gen_dir} ${gen_dir}
        COMMAND tar xvf ${CMAKE_CURRENT_SOURCE_DIR}/bins/bins.tar.xz -C ${gen_dir}
        COMMAND touch ${gen_dir}/.stamp_extracted)


add_custom_target(bins-extracted DEPENDS ${gen_dir}/.stamp_extracted)


macro(model_libs model)

set(lib_dir         ${gen_dir}/lib/${model})
set(pad_dir         ${gen_dir}/pad/${model})
set(src_dir         ${gen_dir}/src/${model})
set(imgs_src        ${src_dir}/imgs.xz.c)

## Images ##
hexify(${CMAKE_CURRENT_SOURCE_DIR}/bins/imgs.xz ${imgs_src} imgs_xz)

# Raw binary files from TVM
set(lib_bin         ${bins_dir}/${model}/graphlib.tar)
set(graph_bin       ${bins_dir}/${model}/graph.json)
set(params_bin      ${bins_dir}/${model}/params.bin)

# Padded binary files
set(graph_bin_pad   ${pad_dir}/graph.json.pad)
set(params_bin_pad  ${pad_dir}/params.bin.pad)

# Source files
set(graph_src       ${src_dir}/graph.json.c)
set(params_src      ${src_dir}/params.bin.c)

# Aligned source files
set(graph_src_pad   ${src_dir}/graph.json.pad.c)
set(params_src_pad  ${src_dir}/params.bin.pad.c)


# The operators are typically linked directly into downstream consumers, so don't need to be padded/hexified
add_custom_command(OUTPUT ${lib_dir}/.stamp_extracted DEPENDS bins-extracted
        BYPRODUCTS ${lib_dir}/lib0.o ${lib_dir}/lib1.o ${lib_dir}/devc.o
        COMMAND mkdir -p ${lib_dir}
        COMMAND tar -xf ${lib_bin} -C ${lib_dir}
        COMMAND touch ${lib_dir}/.stamp_extracted)

add_custom_target(libs-extracted-${model} DEPENDS ${lib_dir}/.stamp_extracted)

## Graph ##
pad(${graph_bin} ${graph_bin_pad} 0x1000)
hexify_aligned(${graph_bin_pad} ${graph_src_pad} graph_json_pad 0x1000)
hexify(${graph_bin} ${graph_src} graph_json)

## Params ##
pad(${params_bin} ${params_bin_pad} 0x1000)
hexify_aligned(${params_bin_pad} ${params_src_pad} params_bin_pad 0x1000)
hexify(${params_bin} ${params_src} params_bin)

## Final targets ##
add_library(${model} STATIC ${graph_src_pad} ${graph_src} ${params_src_pad} ${params_src} ${imgs_src})
target_compile_options(${model} PRIVATE -ffunction-sections -fdata-sections)

add_library(${model}-ops STATIC ${lib_dir}/lib0.o ${lib_dir}/lib1.o ${lib_dir}/devc.o)
target_compile_options(${model}-ops PRIVATE -ffunction-sections -fdata-sections)
set_target_properties(${model}-ops PROPERTIES LINKER_LANGUAGE C)
add_dependencies(${model}-ops libs-extracted-${model})

endmacro(model_libs)

##############
## Enclaves ##
##############

# This macro handles the common parts for every VTA enclave. The names of any eapp targets
# are passed in ARGN

macro(add_vta_example name suffix exconfig msgpassconfig)
    # Generate host functionality, including runner script
    add_executable(vta-${name}-${suffix}-runner ${vta_src_dir}/host/host.cpp)
    vta_default_dirs(vta-${name}-${suffix}-runner ${exconfig} ${msgpassconfig})
    target_link_libraries(vta-${name}-${suffix}-runner ${KEYSTONE_LIB_HOST} ${KEYSTONE_LIB_EDGE})
    set_target_properties(vta-${name}-${suffix}-runner PROPERTIES OUTPUT_NAME vta-${name}-runner)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vta-${name}-runner
            COMMAND true
            DEPENDS vta-${name}-${suffix}-runner )

    set(vta-${name}-${suffix}-host-bins ${CMAKE_CURRENT_BINARY_DIR}/vta-${name}-runner)

    add_custom_target(vta-${name}-${suffix}-runner-script
            BYPRODUCTS ${gen_dir}/run-${name}.sh
            DEPENDS ${vta_src_dir}/scripts/run-${name}.sh
            COMMAND mkdir -p ${gen_dir}
            COMMAND cp ${vta_src_dir}/scripts/run-${name}.sh ${gen_dir}/run-${name}.sh)

    foreach(dep IN ITEMS ${ARGN})
        list(APPEND vta-${name}-${suffix}-eapp-bins ${CMAKE_CURRENT_BINARY_DIR}/${dep})
    endforeach(dep)

    add_eyrie_runtime(vta-${name}-${suffix}-eyrie
            ${eyrie_plugins}
            .options_log eyrie-rt)

    add_keystone_package(vta-${name}-${suffix}-package
            "vta-${name}-${suffix}.ke"
            "./run-${name}.sh"
            # Eyrie files
            .options_log eyrie-rt
            # Host runners
            ${vta-${name}-${suffix}-host-bins} ${gen_dir}/run-${name}.sh
            # Eapp bins
            ${vta-${name}-${suffix}-eapp-bins})

    add_dependencies(vta-${name}-${suffix}-package vta-${name}-${suffix}-eyrie vta-${name}-${suffix}-runner-script)
    add_dependencies(examples vta-${name}-${suffix}-package)
endmacro()

macro(vta_default_dirs target exconfig msgpassconfig)
    target_include_directories(${target} PRIVATE ${vta_src_dir}/include ${vta_src_dir}/util ${BUILDROOT_HOST_DIR}/usr/include ${BUILDROOT_TARGET_DIR}/usr/include)
    target_link_directories(${target} PRIVATE ${BUILDROOT_HOST_DIR}/usr/lib/tvm ${BUILDROOT_TARGET_DIR}/usr/lib)

    if(${exconfig} STREQUAL setup_teardown)
        target_compile_definitions(${target} PRIVATE SETUP_TEARDOWN_ONLY)
    endif()

    if(${msgpassconfig} STREQUAL use_mapped)
        target_compile_definitions(${target} PRIVATE USE_MAPPED=1)
    endif()

    target_compile_definitions(${target} PRIVATE MODEL="${VTA_MODEL}")
endmacro(vta_default_dirs)

## Shared functionality library ##

add_library(vta-shared STATIC ${CMAKE_CURRENT_SOURCE_DIR}/eapp/shared/platform.cc ${CMAKE_CURRENT_SOURCE_DIR}/eapp/shared/images.cc)
vta_default_dirs(vta-shared 0 0)
target_compile_options(vta-shared PRIVATE
        -ffunction-sections -fdata-sections -mcmodel=medany)

# Various examples

foreach(model resnet18_v2 resnet34_v2 resnet50_v2 resnet101_v2)
    model_libs(${model})

    foreach(exconfig inference setup_teardown)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/eapp/direct ${CMAKE_CURRENT_BINARY_DIR}/eapp/direct-${model}-${exconfig})
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/eapp/linux_owned ${CMAKE_CURRENT_BINARY_DIR}/eapp/linux_owned-${model}-${exconfig})
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/eapp/export ${CMAKE_CURRENT_BINARY_DIR}/eapp/export-${model}-${exconfig})

        set(msgpassconfig use_mapped)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/eapp/msg-pass ${CMAKE_CURRENT_BINARY_DIR}/eapp/msg-pass-${model}-${exconfig}-${msgpassconfig})

        set(msgpassconfig use_incremental)
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/eapp/msg-pass ${CMAKE_CURRENT_BINARY_DIR}/eapp/msg-pass-${model}-${exconfig}-${msgpassconfig})
    endforeach ()
endforeach ()


