project(keystone-eapp C ASM)

set(CFLAGS      "-Wall -Werror -mcmodel=medany")
set(LDFLAGS     "-static")

set(SOURCE_FILES
  callee.c
  encret.s
  syscall.c
  )

set(INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include/app)

set(CMAKE_C_FLAGS          "${CMAKE_C_FLAGS} ${CFLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${LDFLAGS}")

include_directories(${INCLUDE_DIRS})

add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES DEFINE_SYMBOL "")

install(TARGETS ${PROJECT_NAME} DESTINATION ${out_dir}/lib)
install(DIRECTORY ${INCLUDE_DIRS} DESTINATION ${out_dir}/include)
