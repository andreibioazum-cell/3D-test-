/* Инстанс/устройство/swapchain/оффскрин: константы, всё состояние бэкенда и forward-объявления.
 * Часть Vulkan-бэкенда: включается из vulkan_backend.inc, всё собирается
 * одним translation unit'ом (graphics.c), поэтому состояние общее. */

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <time.h>
#include "native/graphics/shaders_spirv.inc"

#define DS_VK_MAX_SW_IMAGES 8
#define DS_VK_FRAMES_IN_FLIGHT 2
#define DS_VK_DESC_SETS_PER_POOL 96

/* Логирование результата Vulkan: 1 - успех, 0 - ошибка (с сообщением в лог). */
static int ds_vk_check_result(VkResult r, const char *what) {
    if (r == VK_SUCCESS) return 1;
    ds_log_err("vulkan: %s failed (%d)", what, (int)r);
    return 0;
}

/* --- состояние бэкенда --- */

static VkInstance vk_instance;
static VkSurfaceKHR vk_surface;
static ANativeWindow *vk_window;
static VkPhysicalDevice vk_phys;
static VkDevice vk_device;
static VkQueue vk_queue;
static uint32_t vk_queue_family;
static int vk_ready;
static int vk_need_recreate;
static int vk_blit_ok = 1;
/* Есть ли у формата swapchain линейная фильтрация для blit: растягивание
 * уменьшенного оффскрина с LINEAR выглядит мягко, а не «пикселями». */
static int vk_blit_linear = 0;

static VkSwapchainKHR vk_swap;
static uint32_t vk_sw_count;
static VkImage vk_sw_images[DS_VK_MAX_SW_IMAGES];
static VkImageView vk_sw_views[DS_VK_MAX_SW_IMAGES];
static VkSemaphore vk_sw_present[DS_VK_MAX_SW_IMAGES]; /* "кадр отрисован" */
static VkFormat vk_sw_format = VK_FORMAT_R8G8B8A8_UNORM;
static VkExtent2D vk_sw_extent;
static uint32_t vk_sw_index;
static int vk_image_held; /* кадр захвачен, но ещё не показан (cancel_frame) */

static VkRenderPass vk_renderpass;
static VkFormat vk_rp_format; /* формат swapchain, под который собраны render pass и конвейеры */
static VkPipelineLayout vk_layout_solid;
static VkPipelineLayout vk_layout_image;
static VkPipelineLayout vk_layout_tint;
static VkPipeline vk_pipe_solid;
static VkPipeline vk_pipe_image;
static VkPipeline vk_pipe_tint;
/* roundrect в софтрендере рисуется fill_span'ом - заменой пикселей без
 * смешивания (альфа примитива пишется в кадр как есть). Отдельный конвейер
 * с отключенным смешиванием повторяет это поведение один в один. */
static VkPipeline vk_pipe_replace;
static VkDescriptorSetLayout vk_desc_layout;
static VkSampler vk_sampler;

/* Оффскрин = виртуальный экран; очищается load-op'ом каждый кадр. */
static VkImage vk_off_image;
static VkDeviceMemory vk_off_mem;
static VkImageView vk_off_view;
static VkFramebuffer vk_off_fb;
static VkFormat vk_off_format = VK_FORMAT_R8G8B8A8_UNORM;
static uint32_t vk_off_w, vk_off_h;
/* Логический размер кадра (физические пиксели окна): в этих координатах живут
 * скрипты, поэтому вершинный шейдер делит на них, а не на размер оффскрина —
 * апскейл уменьшает число пикселей растра, но не увеличивает интерфейс. */
static uint32_t vk_log_w, vk_log_h;

typedef struct {
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkFence fence;
    VkSemaphore image_available; /* "кадр захвачен" */
    VkBuffer vbuf, ibuf;
    VkDeviceMemory vmem, imem;
    GeoVert *vmap;
    uint16_t *imap;
    size_t vcap, icap; /* ёмкость в элементах */
    int submitted;     /* есть незадождаленный submit: фенс надо дождаться */
} DsVkFrame;
static DsVkFrame vk_frames[DS_VK_FRAMES_IN_FLIGHT];
static int vk_frame_cur;
/* Кадр начат (изображение захвачено), но ещё не показан: cancel_frame или
 * ошибка submit оставляют слот в этом состоянии - следующий begin обязан
 * переиспользовать ТОТ ЖЕ слот и то же изображение, иначе submit будет ждать
 * семафор, сигнал от которого уже потерян. */
static int vk_begun;

/* Пулы дескрипторов: по набору на текстуру и один на шрифтовой атлас. */
static VkDescriptorPool *vk_desc_pools;
static size_t vk_desc_pool_n, vk_desc_pool_cap, vk_desc_pool_used;

/* Разовые загрузки текстур (ленивые, редкие) — отдельный пул команд. */
static VkCommandPool vk_upload_pool;
static VkCommandBuffer vk_upload_cmd;
static VkFence vk_upload_fence;

/* GPU-ресурсы шрифтового атласа. */
static VkImage vk_font_image;
static VkDeviceMemory vk_font_memory;
static VkImageView vk_font_view;
static VkDescriptorSet vk_font_desc;

/* Отложенные загрузки: текстуры, созданные скриптом прямо во время кадра,
 * заливаются на GPU в начале end_frame. */
#define DS_VK_PENDING_CAP 64
static Texture *vk_pending_tex[DS_VK_PENDING_CAP];
static int vk_pending_n;
static int vk_pending_font;

/* Пачки треугольников одного конвейера/текстуры подряд — чтобы сотни квадов
 * текста не превращались в сотни draw call'ов. */
typedef struct { int pipeline; const void *desc; uint32_t first; uint32_t count; } GeoBatch;
static GeoBatch *vk_batches;
static size_t vk_batch_n, vk_batch_cap;

static int ds_vk_create_frame_resources(void);
static void ds_vk_destroy_frame_resources(void);
static int ds_vk_recreate_swapchain(void);
static void ds_vk_destroy_swapchain(void);
static int ds_vk_ensure_offscreen(uint32_t w, uint32_t h, VkFormat format);
static void ds_vk_destroy_offscreen(void);
