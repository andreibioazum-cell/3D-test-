cmake_minimum_required(VERSION 3.16)
project(CubicBattle C)
set(CMAKE_C_STANDARD 99)

# Игра — чистый C в game/game_pure.c (DimScript в сборке больше не участвует).

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

add_library(ds_game SHARED "game/game_pure.c" runtime.c main.c ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(ds_game PRIVATE . game ${ANDROID_NDK}/sources/android/native_app_glue)
# vulkan - графический бэкенд рендера (swapchain, оффскрин, спечённые SPIR-V шейдеры).
target_link_libraries(ds_game PRIVATE android log m vulkan)
