cmake_minimum_required(VERSION 3.16)
project(jefetch C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(_POSIX_C_SOURCE=200809L _DEFAULT_SOURCE)
add_compile_options(-Wall -Wextra -Wno-trigraphs -Werror=implicit-function-declaration -Werror=implicit-int)

# Bake the compiler target/libc identity (`JEFETCH_TARGET` / `JEFETCH_LIB`)
# into the binary for `--version`.
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
    COMMAND sh -c "ldd --version 2>/dev/null | head -n 1"
    OUTPUT_VARIABLE JEFETCH_LDD_LINE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(JEFETCH_LDD_LINE MATCHES "GNU libc")
    set(JEFETCH_LIB_DETECTED "glibc")
else()
    set(JEFETCH_LIB_DETECTED "unknown")
endif()
if(JEFETCH_TARGET_TRIPLE MATCHES "musl")
    set(JEFETCH_LIB_DETECTED "musl")
endif()
add_compile_definitions(
    JEFETCH_TARGET="${JEFETCH_TARGET_TRIPLE}"
    JEFETCH_LIB="${JEFETCH_LIB_DETECTED}"
)

find_package(Threads REQUIRED)

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
    src/logo_data.c
)

target_include_directories(jefetch PRIVATE src)
target_link_libraries(jefetch PRIVATE m Threads::Threads)

install(TARGETS jefetch DESTINATION bin)
