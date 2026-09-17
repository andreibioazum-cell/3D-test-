cmake_minimum_required(VERSION 3.16)
project(CubicBattle C)
set(CMAKE_C_STANDARD 99)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(GAME_C "${CMAKE_CURRENT_SOURCE_DIR}/game/game.c")
# Скрипты лежат модулями в game/scripts/ (core/ui/combat/fx) — порядок
# компиляции задаёт gen.py, здесь достаточно просто найти все .ds.
file(GLOB_RECURSE DS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/game/*.ds")

add_custom_command(
    OUTPUT "${GAME_C}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/gen.py"
    DEPENDS ${DS_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/ds_compiler.py" "${CMAKE_CURRENT_SOURCE_DIR}/gen.py"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating game/game.c from DimScript"
)

# Единственная целевая платформа — Android: библиотека libds_game.so, которую
# забирает APK из game/AndroidManifest.xml. Поддержка ПК (Windows и Linux-тестовый
# стенд) из репозитория удалена.
if(NOT ANDROID)
    message(FATAL_ERROR
        "Cubic Battle собирается только под Android. Запусти CMake через инструментальный "
        "файл NDK, например: cmake -B build -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake "
        "-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared. "
        "Готовый APK проще собрать workflow в GitHub Actions.")
endif()

add_library(ds_game SHARED "${GAME_C}" runtime.c main.c ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(ds_game PRIVATE . game ${ANDROID_NDK}/sources/android/native_app_glue)
# vulkan - графический бэкенд рендера (swapchain, оффскрин, спечённые SPIR-V шейдеры).
target_link_libraries(ds_game PRIVATE android log m vulkan)
