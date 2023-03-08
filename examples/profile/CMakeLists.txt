set(profile_send_bin profile-sender)
set(profile_recv_bin profile-receiver)
set(profile_send_src eapp/sender.c eapp/util.c)
set(profile_recv_src eapp/receiver.c eapp/util.c)
set(host_bin profile-runner)
set(host_src host/host.cpp)
set(runner_script run.sh)
set(package_name "profile.ke")
set(package_script "./run.sh")

set(eyrie_plugins "freemem io_syscall linux_syscall env_setup callee thread callee_profile")

# eapp

add_exportable_function(export 0 eapp/export.c)
target_link_libraries(export_sym ${KEYSTONE_LIB_EAPP})

add_executable(${profile_send_bin} ${profile_send_src})
target_link_libraries(${profile_send_bin} "-static" ${KEYSTONE_LIB_EAPP} pthread m export)

add_executable(${profile_recv_bin} ${profile_recv_src})
target_link_libraries(${profile_recv_bin} "-static" ${KEYSTONE_LIB_EAPP} pthread m)

# host

add_executable(${host_bin} ${host_src})
target_link_libraries(${host_bin} pthread ${KEYSTONE_LIB_HOST} ${KEYSTONE_LIB_EDGE})

# runner script
add_custom_target(profile-runner-script
        BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${runner_script}
        DEPENDS ${runner_script}
        COMMAND rsync ${CMAKE_CURRENT_SOURCE_DIR}/${runner_script}
        ${CMAKE_CURRENT_BINARY_DIR}/${runner_script} )

# add target for Eyrie runtime (see keystone.cmake)

set(eyrie_files_to_copy .options_log eyrie-rt)
add_eyrie_runtime(profile-eyrie
        ${eyrie_plugins}
        ${eyrie_files_to_copy})

# add target for packaging (see keystone.cmake)

add_keystone_package(profile-package
        ${package_name}
        ${package_script}
        ${eyrie_files_to_copy} ${profile_send_bin} ${profile_recv_bin} ${host_bin} ${runner_script})

add_dependencies(profile-package profile-eyrie profile-runner-script)

# add package to the top-level target
add_dependencies(examples profile-package)
