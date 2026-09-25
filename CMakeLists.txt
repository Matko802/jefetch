cmake_minimum_required(VERSION 3.16)
project(jefetch C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(_POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
add_compile_options(-Wall -Wextra -Wno-trigraphs)

# Bake the same target/libc identity `build.rs` bakes into the Rust binary
# (`JEFETCH_TARGET` / `JEFETCH_LIB`), so `--version` matches.
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -dumpmachine
    OUTPUT_VARIABLE JEFETCH_TARGET_TRIPLE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT JEFETCH_TARGET_TRIPLE)
    set(JEFETCH_TARGET_TRIPLE "unknown")
endif()
execute_process(
    COMMAND sh -c "echo | ${CMAKE_C_COMPILER} -dM -E - 2>/dev/null | grep -q __GLIBC__ && echo glibc || echo unknown"
    OUTPUT_VARIABLE JEFETCH_LIB_DETECTED
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(JEFETCH_TARGET_TRIPLE MATCHES "musl")
    set(JEFETCH_LIB_DETECTED "musl")
endif()
add_compile_definitions(
    JEFETCH_TARGET="${JEFETCH_TARGET_TRIPLE}"
    JEFETCH_LIB="${JEFETCH_LIB_DETECTED}"
)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(LOGO_GEN_C ${CMAKE_BINARY_DIR}/logo_data.c)

add_custom_command(
    OUTPUT ${LOGO_GEN_C}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/logo_data_to_c.py
            ${CMAKE_SOURCE_DIR}/src/logo/data.rs ${LOGO_GEN_C}
    DEPENDS ${CMAKE_SOURCE_DIR}/scripts/logo_data_to_c.py
            ${CMAKE_SOURCE_DIR}/src/logo/data.rs
    COMMENT "Generating logo table"
)

add_executable(jefetch
    src/main.c
    src/common.c
    src/version.c
    src/json.c
    src/config.c
    src/print.c
    src/detect_util.c
    src/detect_uptime.c
    src/detect_kernel.c
    src/detect_memory.c
    src/detect_de.c
    src/detect_user.c
    src/detect_users.c
    src/detect_os.c
    src/detect_battery.c
    src/detect_brightness.c
    src/detect_board.c
    src/detect_lm.c
    src/detect_initsystem.c
    src/detect_wm.c
    src/detect_dns.c
    src/detect_disk.c
    src/detect_shell.c
    src/detect_theme.c
    src/detect_packages.c
    src/detect_terminal.c
    src/detect_display.c
    src/detect_wayland.c
    src/detect_cpu.c
    src/detect_localip.c
    src/detect_gpu.c
    src/detect_wifi.c
    src/detect_publicip.c
    src/pa.c
    src/sharkvis_sync.c
    src/logo.c
    src/logo_image.c
    src/logo_graphics.c
    src/img_bmp_gif.c
    src/img_inflate.c
    src/img_png.c
    src/img_jpg.c
    src/img_load.c
    src/img_b64.c
    src/anim.c
    src/modules.c
    src/app.c
    ${LOGO_GEN_C}
)

target_include_directories(jefetch PRIVATE src ${CMAKE_BINARY_DIR})
target_link_libraries(jefetch PRIVATE m Threads::Threads)

install(TARGETS jefetch DESTINATION bin)
