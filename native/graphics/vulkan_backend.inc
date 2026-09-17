/* Vulkan-бэкенд рендера Cubic Battle: инстанс/поверхность/устройство,
 * swapchain, оффскрин-таргет, конвейеры со спечёнными SPIR-V шейдерами,
 * загрузка текстур на GPU и покадровый цикл begin/end/cancel.
 *
 * Картинка строится так: скрипт складывает команды (lifecycle.inc), в конце
 * кадра geometry.inc раскладывает их на треугольники, бэкенд рисует их в
 * оффскрин RGBA8-таргет размером window/scale (апскейл из настроек), затем
 * nearest-blit'ит результат в кадр swapchain на всё окно. Координаты скриптов
 * при этом остаются физическими пикселями окна: вершинный шейдер получает
 * логический размер, поэтому апскейл уменьшает число пикселей растра, но не
 * увеличивает интерфейс.
 *
 * Реализация разбита по ответственности (лимит 450 строк на файл); порядок
 * включений важен: состояние объявляется раньше, чем используется. */
#include "native/graphics/vulkan_state.inc"
#include "native/graphics/vulkan_upload.inc"
#include "native/graphics/vulkan_pipelines.inc"
#include "native/graphics/vulkan_swapchain.inc"
#include "native/graphics/vulkan_record.inc"
#include "native/graphics/vulkan_frame_loop.inc"
