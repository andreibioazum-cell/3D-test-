/* Vulkan-бэкенд рендера Cubic Battle: инстанс/поверхность/устройство,
 * swapchain, оффскрин-таргет, конвейеры со спечёнными SPIR-V шейдерами,
 * загрузка текстур на GPU и покадровый цикл begin/end/cancel.
 *
 * Картинка строится так: скрипт складывает команды (lifecycle.inc), в конце
 * кадра geometry.inc раскладывает их на треугольники, бэкенд рисует их в
 * оффскрин RGBA8-таргет размером виртуального экрана (screen_w x screen_h),
 * затем nearest-blit'ит результат в кадр swapchain — так работает апскейл
 * из настроек (пиксельная картинка), только теперь на GPU и без
 * ANativeWindow_lock/unlockAndPost. */
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

/* --- память и буферы --- */

static uint32_t ds_vk_find_mem_type(uint32_t req, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(vk_phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((req & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    return UINT32_MAX;
}

static int ds_vk_alloc_device_memory(VkMemoryRequirements mr, VkDeviceMemory *out) {
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = ds_vk_find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX) { ds_log_err("vulkan: no device-local memory type"); return 0; }
    return ds_vk_check_result(vkAllocateMemory(vk_device, &ai, NULL, out), "vkAllocateMemory");
}

static int ds_vk_create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags props, VkBuffer *buf, VkDeviceMemory *mem,
                               void **mapped) {
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = size; bi.usage = usage; bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!ds_vk_check_result(vkCreateBuffer(vk_device, &bi, NULL, buf), "vkCreateBuffer")) return 0;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(vk_device, *buf, &mr);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = ds_vk_find_mem_type(mr.memoryTypeBits, props);
    if (ai.memoryTypeIndex == UINT32_MAX) { ds_log_err("vulkan: no host memory type"); return 0; }
    if (!ds_vk_check_result(vkAllocateMemory(vk_device, &ai, NULL, mem), "vkAllocateMemory")) return 0;
    if (!ds_vk_check_result(vkBindBufferMemory(vk_device, *buf, *mem, 0), "vkBindBufferMemory")) return 0;
    if (mapped && !ds_vk_check_result(vkMapMemory(vk_device, *mem, 0, size, 0, mapped), "vkMapMemory")) return 0;
    return 1;
}

static void ds_vk_destroy_buffer(VkBuffer *buf, VkDeviceMemory *mem) {
    if (!vk_device) return;
    if (*buf != VK_NULL_HANDLE) vkDestroyBuffer(vk_device, *buf, NULL);
    if (*mem != VK_NULL_HANDLE) vkFreeMemory(vk_device, *mem, NULL);
    *buf = VK_NULL_HANDLE; *mem = VK_NULL_HANDLE;
}

/* --- дескрипторы (combined image sampler на текстуру) --- */

static int ds_vk_new_descriptor_set(VkImageView view, VkDescriptorSet *out) {
    *out = VK_NULL_HANDLE;
    if (vk_desc_pool_used >= DS_VK_DESC_SETS_PER_POOL || vk_desc_pool_n == 0) {
        if (vk_desc_pool_n == vk_desc_pool_cap) {
            size_t cap = vk_desc_pool_cap ? vk_desc_pool_cap * 2 : 4;
            VkDescriptorPool *np = (VkDescriptorPool *)realloc(vk_desc_pools, cap * sizeof(*np));
            if (!np) { ds_log_err("vulkan: out of memory for descriptor pools"); return 0; }
            vk_desc_pools = np; vk_desc_pool_cap = cap;
        }
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DS_VK_DESC_SETS_PER_POOL };
        VkDescriptorPoolCreateInfo pi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pi.maxSets = DS_VK_DESC_SETS_PER_POOL;
        pi.poolSizeCount = 1; pi.pPoolSizes = &ps;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        if (!ds_vk_check_result(vkCreateDescriptorPool(vk_device, &pi, NULL, &pool), "vkCreateDescriptorPool")) return 0;
        vk_desc_pools[vk_desc_pool_n++] = pool;
        vk_desc_pool_used = 0;
    }
    VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    ai.descriptorPool = vk_desc_pools[vk_desc_pool_n - 1];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &vk_desc_layout;
    if (!ds_vk_check_result(vkAllocateDescriptorSets(vk_device, &ai, out), "vkAllocateDescriptorSets")) return 0;
    VkDescriptorImageInfo di = { vk_sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet wr = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    wr.dstSet = *out;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &di;
    vkUpdateDescriptorSets(vk_device, 1, &wr, 0, NULL);
    vk_desc_pool_used++;
    return 1;
}

/* --- загрузка изображений на GPU --- */

/* Заливка staging -> image одной разовой командой с ожиданием. Вызывается
 * редко (ленивая загрузка текстур и атласа шрифта), поэтому простота важнее
 * параллельности. */
static int ds_vk_upload_image(uint32_t w, uint32_t h, const uint32_t *pixels,
                              VkImage *out_image, VkDeviceMemory *out_mem,
                              VkImageView *out_view, VkDescriptorSet *out_desc) {
    *out_image = VK_NULL_HANDLE; *out_mem = VK_NULL_HANDLE;
    *out_view = VK_NULL_HANDLE; *out_desc = VK_NULL_HANDLE;
    VkDeviceSize size = (VkDeviceSize)w * h * 4;
    if (size == 0 || !pixels) { ds_log_err("vulkan: empty upload"); return 0; }

    VkBuffer staging = VK_NULL_HANDLE; VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    if (!ds_vk_create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             &staging, &staging_mem, NULL)) return 0;
    void *map = NULL;
    if (!ds_vk_check_result(vkMapMemory(vk_device, staging_mem, 0, size, 0, &map), "vkMapMemory(staging)")) {
        ds_vk_destroy_buffer(&staging, &staging_mem); return 0;
    }
    memcpy(map, pixels, (size_t)size);
    vkUnmapMemory(vk_device, staging_mem);

    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent.width = w; ii.extent.height = h; ii.extent.depth = 1;
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ds_vk_check_result(vkCreateImage(vk_device, &ii, NULL, out_image), "vkCreateImage")) {
        ds_vk_destroy_buffer(&staging, &staging_mem); return 0;
    }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(vk_device, *out_image, &mr);
    if (!ds_vk_alloc_device_memory(mr, out_mem)) goto fail;
    if (!ds_vk_check_result(vkBindImageMemory(vk_device, *out_image, *out_mem, 0), "vkBindImageMemory")) goto fail;

    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!ds_vk_check_result(vkResetCommandBuffer(vk_upload_cmd, 0), "vkResetCommandBuffer(upload)")) goto fail;
    if (!ds_vk_check_result(vkBeginCommandBuffer(vk_upload_cmd, &bi), "vkBeginCommandBuffer(upload)")) goto fail;
    VkImageMemoryBarrier bar = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = *out_image;
    bar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar.subresourceRange.levelCount = 1; bar.subresourceRange.layerCount = 1;
    /* UNDEFINED -> TRANSFER_DST: запись копии. */
    VkImageMemoryBarrier to_dst = bar;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(vk_upload_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_dst);
    VkBufferImageCopy region = { 0 };
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = w; region.imageExtent.height = h; region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(vk_upload_cmd, staging, *out_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    /* TRANSFER_DST -> SHADER_READ_ONLY: чтение во фрагментном шейдере. */
    VkImageMemoryBarrier to_read = bar;
    to_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(vk_upload_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_read);
    if (!ds_vk_check_result(vkEndCommandBuffer(vk_upload_cmd), "vkEndCommandBuffer(upload)")) goto fail;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.commandBufferCount = 1; si.pCommandBuffers = &vk_upload_cmd;
    if (!ds_vk_check_result(vkQueueSubmit(vk_queue, 1, &si, vk_upload_fence), "vkQueueSubmit(upload)")) goto fail;
    if (!ds_vk_check_result(vkWaitForFences(vk_device, 1, &vk_upload_fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(upload)")) goto fail;
    if (!ds_vk_check_result(vkResetFences(vk_device, 1, &vk_upload_fence), "vkResetFences(upload)")) goto fail;

    ds_vk_destroy_buffer(&staging, &staging_mem);

    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = *out_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (!ds_vk_check_result(vkCreateImageView(vk_device, &vi, NULL, out_view), "vkCreateImageView")) goto fail;
    if (!ds_vk_new_descriptor_set(*out_view, out_desc)) goto fail;
    return 1;
fail:
    ds_vk_destroy_buffer(&staging, &staging_mem);
    if (*out_view != VK_NULL_HANDLE) vkDestroyImageView(vk_device, *out_view, NULL);
    if (*out_mem != VK_NULL_HANDLE) vkFreeMemory(vk_device, *out_mem, NULL);
    if (*out_image != VK_NULL_HANDLE) vkDestroyImage(vk_device, *out_image, NULL);
    *out_image = VK_NULL_HANDLE; *out_mem = VK_NULL_HANDLE; *out_view = VK_NULL_HANDLE;
    *out_desc = VK_NULL_HANDLE;
    return 0;
}

/* Шрифтовый атлас хранится как 8-битное покрытие; для GPU превращаем его в
 * белый RGBA8-тексель с альфой = покрытие (цвет задаёт вершина, см. image.frag). */
static int ds_vk_upload_font_atlas(void) {
    if (!font || vk_font_desc != VK_NULL_HANDLE) return font != NULL;
    int aw = ds_font_aw(font), ah = ds_font_ah(font);
    const uint8_t *alpha = ds_font_alpha(font);
    if (aw <= 0 || ah <= 0 || !alpha) return 0;
    uint32_t *rgba = (uint32_t *)malloc((size_t)aw * ah * sizeof(uint32_t));
    if (!rgba) { ds_log_err("vulkan: out of memory for font atlas"); return 0; }
    for (size_t i = 0; i < (size_t)aw * ah; i++)
        rgba[i] = 0x00ffffffu | ((uint32_t)alpha[i] << 24);
    VkImage img, *imgp = &img;
    VkDeviceMemory mem; VkImageView view; VkDescriptorSet desc;
    int ok = ds_vk_upload_image((uint32_t)aw, (uint32_t)ah, rgba, imgp, &mem, &view, &desc);
    free(rgba);
    if (ok) {
        vk_font_image = img; vk_font_memory = mem; vk_font_view = view; vk_font_desc = desc;
    }
    return ok;
}

static void ds_vk_font_gpu_release(void) {
    if (!vk_device) return;
    if (vk_font_view != VK_NULL_HANDLE) vkDestroyImageView(vk_device, vk_font_view, NULL);
    if (vk_font_image != VK_NULL_HANDLE) vkDestroyImage(vk_device, vk_font_image, NULL);
    if (vk_font_memory != VK_NULL_HANDLE) vkFreeMemory(vk_device, vk_font_memory, NULL);
    vk_font_image = VK_NULL_HANDLE; vk_font_memory = VK_NULL_HANDLE;
    vk_font_view = VK_NULL_HANDLE; vk_font_desc = VK_NULL_HANDLE;
}

static void ds_vk_texture_gpu_release(Texture *t) {
    if (!t || !vk_device) return;
    if (t->gpu.view) vkDestroyImageView(vk_device, (VkImageView)(uintptr_t)t->gpu.view, NULL);
    if (t->gpu.image) vkDestroyImage(vk_device, (VkImage)(uintptr_t)t->gpu.image, NULL);
    if (t->gpu.memory) vkFreeMemory(vk_device, (VkDeviceMemory)(uintptr_t)t->gpu.memory, NULL);
    memset(&t->gpu, 0, sizeof(t->gpu));
}

static void ds_vk_pending_reset(void) { vk_pending_n = 0; vk_pending_font = 0; }

static void ds_vk_pending_texture(Texture *t) {
    if (!t) return;
    for (int i = 0; i < vk_pending_n; i++) if (vk_pending_tex[i] == t) return;
    if (vk_pending_n < DS_VK_PENDING_CAP) vk_pending_tex[vk_pending_n++] = t;
}

/* Отложенные загрузки обрабатываются в начале end_frame. */
static void ds_vk_process_pending_uploads(void) {
    if (!vk_ready) { vk_pending_n = 0; vk_pending_font = 0; return; }
    for (int i = 0; i < vk_pending_n; i++) {
        Texture *t = vk_pending_tex[i];
        if (!t || !t->pixels || t->gpu.uploaded) continue;
        VkImage img; VkDeviceMemory mem; VkImageView view; VkDescriptorSet desc;
        if (ds_vk_upload_image((uint32_t)t->w, (uint32_t)t->h, t->pixels, &img, &mem, &view, &desc)) {
            t->gpu.image = (long long)(intptr_t)img;
            t->gpu.memory = (long long)(intptr_t)mem;
            t->gpu.view = (long long)(intptr_t)view;
            t->gpu.desc = (long long)(intptr_t)desc;
            t->gpu.uploaded = 1;
        } else {
            ds_log_err("vulkan: texture upload failed for '%s'", t->name ? t->name : "?");
        }
    }
    vk_pending_n = 0;
    if (vk_pending_font) {
        vk_pending_font = 0;
        if (!ds_vk_upload_font_atlas())
            ds_log_err("vulkan: font atlas upload failed");
    }
}

/* --- конвейеры --- */

static int ds_vk_create_shader_module(const uint32_t *code, size_t words, VkShaderModule *out) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = words * sizeof(uint32_t);
    ci.pCode = code;
    return ds_vk_check_result(vkCreateShaderModule(vk_device, &ci, NULL, out), "vkCreateShaderModule");
}

static int ds_vk_create_pipeline(VkShaderModule frag, VkPipelineLayout layout, int blend, VkPipeline *out) {
    VkPipelineShaderStageCreateInfo stages[2];
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = VK_NULL_HANDLE;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    if (!ds_vk_create_shader_module(ds_spirv_sprite_vert,
                                    sizeof(ds_spirv_sprite_vert) / sizeof(uint32_t), &stages[0].module))
        return 0;

    VkVertexInputBindingDescription binding = { 0, sizeof(GeoVert), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attrs[3] = {
        { 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GeoVert, x) },
        { 1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(GeoVert, u) },
        { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(GeoVert, c) },
    };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1; vp.scissorCount = 1; /* viewport/scissor задаются динамически */

    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    VkPipelineColorBlendAttachmentState cb = { 0 };
    cb.blendEnable = (VkBool32)(blend ? VK_TRUE : VK_FALSE);
    cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.colorBlendOp = VK_BLEND_OP_ADD;
    cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.alphaBlendOp = VK_BLEND_OP_ADD;
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbs = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cb;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dy.dynamicStateCount = 2; dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pi = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pi.stageCount = 2; pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &dss;
    pi.pColorBlendState = &cbs;
    pi.pDynamicState = &dy;
    pi.layout = layout;
    pi.renderPass = vk_renderpass;
    pi.subpass = 0;
    int ok = ds_vk_check_result(vkCreateGraphicsPipelines(vk_device, VK_NULL_HANDLE, 1, &pi, NULL, out),
                                "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(vk_device, stages[0].module, NULL);
    vkDestroyShaderModule(vk_device, frag, NULL);
    return ok;
}

/* --- оффскрин-таргет --- */

static int ds_vk_ensure_offscreen(uint32_t w, uint32_t h, VkFormat format) {
    if (vk_off_image != VK_NULL_HANDLE && vk_off_w == w && vk_off_h == h && vk_off_format == format) return 1;
    if (vk_device) vkDeviceWaitIdle(vk_device);
    ds_vk_destroy_offscreen();
    VkImageCreateInfo ii = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = w; ii.extent.height = h; ii.extent.depth = 1;
    ii.mipLevels = 1; ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!ds_vk_check_result(vkCreateImage(vk_device, &ii, NULL, &vk_off_image), "vkCreateImage(offscreen)")) return 0;
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(vk_device, vk_off_image, &mr);
    if (!ds_vk_alloc_device_memory(mr, &vk_off_mem)) { ds_vk_destroy_offscreen(); return 0; }
    if (!ds_vk_check_result(vkBindImageMemory(vk_device, vk_off_image, vk_off_mem, 0), "vkBindImageMemory(offscreen)")) {
        ds_vk_destroy_offscreen(); return 0;
    }
    VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vi.image = vk_off_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
    if (!ds_vk_check_result(vkCreateImageView(vk_device, &vi, NULL, &vk_off_view), "vkCreateImageView(offscreen)")) {
        ds_vk_destroy_offscreen(); return 0;
    }
    VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    fi.renderPass = vk_renderpass;
    fi.attachmentCount = 1; fi.pAttachments = &vk_off_view;
    fi.width = w; fi.height = h; fi.layers = 1;
    if (!ds_vk_check_result(vkCreateFramebuffer(vk_device, &fi, NULL, &vk_off_fb), "vkCreateFramebuffer")) {
        ds_vk_destroy_offscreen(); return 0;
    }
    vk_off_w = w; vk_off_h = h; vk_off_format = format;
    return 1;
}

static void ds_vk_destroy_offscreen(void) {
    if (!vk_device) return;
    if (vk_off_fb != VK_NULL_HANDLE) vkDestroyFramebuffer(vk_device, vk_off_fb, NULL);
    if (vk_off_view != VK_NULL_HANDLE) vkDestroyImageView(vk_device, vk_off_view, NULL);
    if (vk_off_image != VK_NULL_HANDLE) vkDestroyImage(vk_device, vk_off_image, NULL);
    if (vk_off_mem != VK_NULL_HANDLE) vkFreeMemory(vk_device, vk_off_mem, NULL);
    vk_off_fb = VK_NULL_HANDLE; vk_off_view = VK_NULL_HANDLE;
    vk_off_image = VK_NULL_HANDLE; vk_off_mem = VK_NULL_HANDLE;
    vk_off_w = vk_off_h = 0;
}

/* --- swapchain --- */

static void ds_vk_destroy_swapchain(void) {
    if (!vk_device) return;
    for (uint32_t i = 0; i < DS_VK_MAX_SW_IMAGES; i++) {
        if (vk_sw_views[i] != VK_NULL_HANDLE) vkDestroyImageView(vk_device, vk_sw_views[i], NULL);
        if (vk_sw_present[i] != VK_NULL_HANDLE) vkDestroySemaphore(vk_device, vk_sw_present[i], NULL);
        vk_sw_views[i] = VK_NULL_HANDLE; vk_sw_present[i] = VK_NULL_HANDLE; vk_sw_images[i] = VK_NULL_HANDLE;
    }
    if (vk_swap != VK_NULL_HANDLE) vkDestroySwapchainKHR(vk_device, vk_swap, NULL);
    vk_swap = VK_NULL_HANDLE;
    vk_sw_count = 0;
    vk_image_held = 0;
}

static int ds_vk_recreate_swapchain(void) {
    if (!vk_surface || !vk_phys) return 0;
    if (vk_device) vkQueueWaitIdle(vk_queue);
    ds_vk_destroy_swapchain();
    VkSurfaceCapabilitiesKHR caps;
    if (!ds_vk_check_result(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_phys, vk_surface, &caps),
                            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) return 0;
    if (caps.currentExtent.width == UINT32_MAX || caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
        return 0; /* окно схлопнулось - попробуем в следующем кадре */

    uint32_t want = caps.minImageCount + 1;
    if (want < 3) want = 3;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;

    /* Формат: обязательно с поддержкой blit (нужен nearest-апскейл); для
     * R8G8B8A8/B8G8R8A8_UNORM это гарантия спецификации. */
    uint32_t nf = 0;
    if (!ds_vk_check_result(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_phys, vk_surface, &nf, NULL),
                            "vkGetPhysicalDeviceSurfaceFormatsKHR")) return 0;
    if (nf == 0) { ds_log_err("vulkan: surface has no formats"); return 0; }
    VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)malloc(nf * sizeof(*formats));
    if (!formats) return 0;
    if (!ds_vk_check_result(vkGetPhysicalDeviceSurfaceFormatsKHR(vk_phys, vk_surface, &nf, formats),
                            "vkGetPhysicalDeviceSurfaceFormatsKHR")) { free(formats); return 0; }
    VkSurfaceFormatKHR chosen = formats[0];
    for (uint32_t i = 0; i < nf; i++) {
        VkFormat f = formats[i].format;
        if (formats[i].colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
        if (f == VK_FORMAT_R8G8B8A8_UNORM || f == VK_FORMAT_B8G8R8A8_UNORM) { chosen = formats[i]; break; }
    }
    free(formats);
    vk_sw_format = chosen.format;

    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(vk_phys, vk_sw_format, &fp);
    vk_blit_ok = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
                 (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
    if (!vk_blit_ok)
        ds_log_err("vulkan: format %d lacks blit support, upscale degrades to copy", (int)vk_sw_format);

    VkSwapchainCreateInfoKHR si = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    si.surface = vk_surface;
    si.minImageCount = want;
    si.imageFormat = vk_sw_format;
    si.imageColorSpace = chosen.colorSpace;
    si.imageExtent = caps.currentExtent;
    si.imageArrayLayers = 1;
    si.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    si.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    si.preTransform = caps.currentTransform;
    si.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    si.presentMode = VK_PRESENT_MODE_FIFO_KHR; /* поддерживается всегда, бережёт батарею */
    si.clipped = VK_TRUE;
    si.oldSwapchain = VK_NULL_HANDLE;
    if (!ds_vk_check_result(vkCreateSwapchainKHR(vk_device, &si, NULL, &vk_swap), "vkCreateSwapchainKHR")) return 0;
    vk_sw_count = DS_VK_MAX_SW_IMAGES;
    if (!ds_vk_check_result(vkGetSwapchainImagesKHR(vk_device, vk_swap, &vk_sw_count, vk_sw_images),
                            "vkGetSwapchainImagesKHR")) return 0;
    for (uint32_t i = 0; i < vk_sw_count; i++) {
        VkImageViewCreateInfo vi = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = vk_sw_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = vk_sw_format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1; vi.subresourceRange.layerCount = 1;
        if (!ds_vk_check_result(vkCreateImageView(vk_device, &vi, NULL, &vk_sw_views[i]), "vkCreateImageView(swapchain)")) return 0;
        VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (!ds_vk_check_result(vkCreateSemaphore(vk_device, &sci, NULL, &vk_sw_present[i]), "vkCreateSemaphore(present)")) return 0;
    }
    vk_sw_extent = caps.currentExtent;
    vk_need_recreate = 0;
    vk_image_held = 0;
    ds_log("vulkan: swapchain %ux%u, %u images, format %d", vk_sw_extent.width, vk_sw_extent.height,
           (unsigned)vk_sw_count, (int)vk_sw_format);
    return 1;
}

/* --- кадры в полёте --- */

static void ds_vk_destroy_frame_resources(void) {
    for (int i = 0; i < DS_VK_FRAMES_IN_FLIGHT; i++) {
        DsVkFrame *f = &vk_frames[i];
        if (!vk_device) break;
        if (f->pool != VK_NULL_HANDLE) vkDestroyCommandPool(vk_device, f->pool, NULL);
        if (f->fence != VK_NULL_HANDLE) vkDestroyFence(vk_device, f->fence, NULL);
        if (f->image_available != VK_NULL_HANDLE) vkDestroySemaphore(vk_device, f->image_available, NULL);
        ds_vk_destroy_buffer(&f->vbuf, &f->vmem);
        ds_vk_destroy_buffer(&f->ibuf, &f->imem);
        f->cmd = VK_NULL_HANDLE; f->fence = VK_NULL_HANDLE; f->image_available = VK_NULL_HANDLE;
        f->vmap = NULL; f->imap = NULL; f->vcap = 0; f->icap = 0;
    }
    vk_frame_cur = 0;
}

static int ds_vk_create_frame_resources(void) {
    ds_vk_destroy_frame_resources();
    for (int i = 0; i < DS_VK_FRAMES_IN_FLIGHT; i++) {
        DsVkFrame *f = &vk_frames[i];
        VkCommandPoolCreateInfo pi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = vk_queue_family;
        if (!ds_vk_check_result(vkCreateCommandPool(vk_device, &pi, NULL, &f->pool), "vkCreateCommandPool(frame)")) return 0;
        VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        ai.commandPool = f->pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (!ds_vk_check_result(vkAllocateCommandBuffers(vk_device, &ai, &f->cmd), "vkAllocateCommandBuffers")) return 0;
        VkFenceCreateInfo fi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; /* первый проход не ждёт */
        if (!ds_vk_check_result(vkCreateFence(vk_device, &fi, NULL, &f->fence), "vkCreateFence(frame)")) return 0;
        VkSemaphoreCreateInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if (!ds_vk_check_result(vkCreateSemaphore(vk_device, &si, NULL, &f->image_available), "vkCreateSemaphore(frame)")) return 0;
        /* Буферы геометрии растут по мере надобности; стартуем с разумного. */
        if (!ds_vk_create_buffer(256 * 1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &f->vbuf, &f->vmem, (void **)&f->vmap)) return 0;
        f->vcap = 256 * 1024 / sizeof(GeoVert);
        if (!ds_vk_create_buffer(384 * 1024, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &f->ibuf, &f->imem, (void **)&f->imap)) return 0;
        f->icap = 384 * 1024 / sizeof(uint16_t);
    }
    return 1;
}

static int ds_vk_frame_ensure_capacity(DsVkFrame *f, size_t verts, size_t idx) {
    if (verts > f->vcap) {
        size_t cap = f->vcap * 2;
        if (cap < verts) cap = verts;
        if (cap > SIZE_MAX / sizeof(GeoVert)) return 0;
        VkBuffer nb; VkDeviceMemory nm; GeoVert *map;
        if (!ds_vk_create_buffer((VkDeviceSize)cap * sizeof(GeoVert), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &nb, &nm, (void **)&map)) return 0;
        ds_vk_destroy_buffer(&f->vbuf, &f->vmem);
        f->vbuf = nb; f->vmem = nm; f->vmap = map; f->vcap = cap;
    }
    if (idx > f->icap) {
        size_t cap = f->icap * 2;
        if (cap < idx) cap = idx;
        if (cap > SIZE_MAX / sizeof(uint16_t)) return 0;
        VkBuffer nb; VkDeviceMemory nm; uint16_t *map;
        if (!ds_vk_create_buffer((VkDeviceSize)cap * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                 &nb, &nm, (void **)&map)) return 0;
        ds_vk_destroy_buffer(&f->ibuf, &f->imem);
        f->ibuf = nb; f->imem = nm; f->imap = map; f->icap = cap;
    }
    return 1;
}

/* --- разбор команд скрипта в геометрию и пачки --- */

static int ds_vk_batch_push(int pipeline, const void *desc, size_t first, size_t count) {
    if (count == 0) return 1;
    if (vk_batch_n == vk_batch_cap) {
        size_t cap = vk_batch_cap ? vk_batch_cap * 2 : 256;
        if (cap > SIZE_MAX / sizeof(GeoBatch)) return 0;
        GeoBatch *nb = (GeoBatch *)realloc(vk_batches, cap * sizeof(*nb));
        if (!nb) { ds_log_err("out of memory for draw batches"); return 0; }
        vk_batches = nb; vk_batch_cap = cap;
    }
    GeoBatch *b = &vk_batches[vk_batch_n++];
    b->pipeline = pipeline; b->desc = desc;
    b->first = (uint32_t)first; b->count = (uint32_t)count;
    return 1;
}

static int ds_vk_build_geometry(void) {
    geo_reset();
    vk_batch_n = 0;
    const void *font_desc = vk_font_desc ? (const void *)(uintptr_t)vk_font_desc : NULL;
    for (size_t i = 0; i < cmd_n; i++) {
        DSCmd *c = &cmds[i];
        int pipeline = 0;
        const void *desc = NULL;
        size_t is = geo_in;
        switch (c->t) {
            case DS_CMD_RECT:
                geo_rect(c->v.rc.x, c->v.rc.y, c->v.rc.w, c->v.rc.h, c->v.rc.c);
                break;
            case DS_CMD_ROUND:
                pipeline = 3; /* fill_span: замена, без смешивания */
                desc = NULL;
                geo_roundrect(c->v.rr.x, c->v.rr.y, c->v.rr.w, c->v.rr.h, c->v.rr.r, c->v.rr.c);
                break;
            case DS_CMD_CIRCLE:
                geo_circle(c->v.ci.x, c->v.ci.y, c->v.ci.r, c->v.ci.c);
                break;
            case DS_CMD_RING:
                geo_ring(c->v.rg.x, c->v.rg.y, c->v.rg.r, c->v.rg.th, c->v.rg.c);
                break;
            case DS_CMD_LINE:
                geo_line(c->v.ln.x1, c->v.ln.y1, c->v.ln.x2, c->v.ln.y2, c->v.ln.th, c->v.ln.c);
                break;
            case DS_CMD_TEX: {
                Texture *t = c->v.tx.tx;
                if (t && t->gpu.uploaded && t->gpu.desc) {
                    pipeline = 1;
                    desc = (const void *)(uintptr_t)t->gpu.desc;
                    geo_tex(c->v.tx.x, c->v.tx.y, c->v.tx.a, c->v.tx.sc,
                            (float)t->w, (float)t->h, 0xffffffffu);
                }
                break;
            }
            case DS_CMD_TEX_TINT: {
                Texture *t = c->v.tx2.tx;
                if (t && t->gpu.uploaded && t->gpu.desc) {
                    /* Силуэт по альфа-каналу текстуры, залитый tint (как было
                     * у софтрендера): цвет = tint.rgb, альфа = tex.a * tint.a. */
                    pipeline = 2;
                    desc = (const void *)(uintptr_t)t->gpu.desc;
                    geo_tex(c->v.tx2.x, c->v.tx2.y, c->v.tx2.a, c->v.tx2.sc,
                            (float)t->w, (float)t->h, c->v.tx2.c);
                }
                break;
            }
            case DS_CMD_TEXT:
                if (font && font_desc) {
                    pipeline = 1;
                    desc = font_desc;
                    geo_text(c->v.tt.s, c->v.tt.x, c->v.tt.y, c->v.tt.c, c->v.tt.sc, font);
                }
                break;
        }
        if (!ds_vk_batch_push(pipeline, desc, is, geo_in - is)) return 0;
    }
    return 1;
}

/* Запись пачек в командный буфер: подряд идущие пачки одного конвейера и
 * одной текстуры сливаются в один vkCmdDrawIndexed. */
static void ds_vk_record_draws(VkCommandBuffer cmd) {
    if (vk_off_w == 0 || vk_off_h == 0) return;
    float pc[4] = { 2.0f / (float)vk_off_w, 2.0f / (float)vk_off_h, -1.0f, -1.0f };
    int cur_pipe = -1;
    const void *cur_desc = NULL;
    size_t i = 0;
    while (i < vk_batch_n) {
        int pipe = vk_batches[i].pipeline;
        const void *desc = (pipe == 1 || pipe == 2) ? vk_batches[i].desc : NULL;
        uint32_t first = vk_batches[i].first, count = 0;
        size_t j = i;
        while (j < vk_batch_n && vk_batches[j].pipeline == pipe &&
               (desc == NULL || vk_batches[j].desc == desc)) {
            count += vk_batches[j].count;
            j++;
        }
        if (pipe != cur_pipe) {
            VkPipelineLayout layout = pipe == 0 ? vk_layout_solid
                                    : pipe == 1 ? vk_layout_image
                                    : pipe == 2 ? vk_layout_tint : vk_layout_solid;
            VkPipeline pipeline = pipe == 0 ? vk_pipe_solid
                                : pipe == 1 ? vk_pipe_image
                                : pipe == 2 ? vk_pipe_tint : vk_pipe_replace;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
            cur_pipe = pipe;
            cur_desc = NULL;
        }
        if (desc && desc != cur_desc) {
            VkDescriptorSet set = (VkDescriptorSet)(uintptr_t)desc;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipe == 1 ? vk_layout_image : vk_layout_tint,
                                    0, 1, &set, 0, NULL);
            cur_desc = desc;
        }
        vkCmdDrawIndexed(cmd, count, 1, first, 0, 0);
        i = j;
    }
}

/* --- вывод кадра: blit оффскрина в swapchain --- */

static void ds_vk_record_present_blit(VkCommandBuffer cmd) {
    VkImage sw_image = vk_sw_images[vk_sw_index];
    VkImageMemoryBarrier to_dst = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_dst.image = sw_image;
    to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_dst.subresourceRange.levelCount = 1; to_dst.subresourceRange.layerCount = 1;
    to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_dst);
    if (vk_blit_ok) {
        VkImageBlit bl;
        memset(&bl, 0, sizeof(bl));
        bl.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bl.srcSubresource.layerCount = 1;
        bl.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bl.dstSubresource.layerCount = 1;
        bl.srcOffsets[1].x = (int32_t)vk_off_w;
        bl.srcOffsets[1].y = (int32_t)vk_off_h;
        bl.srcOffsets[1].z = 1;
        bl.dstOffsets[1].x = (int32_t)vk_sw_extent.width;
        bl.dstOffsets[1].y = (int32_t)vk_sw_extent.height;
        bl.dstOffsets[1].z = 1;
        vkCmdBlitImage(cmd, vk_off_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       sw_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_NEAREST);
    } else {
        /* Запасной путь без blit: попиксельная копия 1:1 (без апскейла). */
        VkImageCopy cp;
        memset(&cp, 0, sizeof(cp));
        cp.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cp.srcSubresource.layerCount = 1;
        cp.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        cp.dstSubresource.layerCount = 1;
        cp.extent.width = vk_off_w < vk_sw_extent.width ? vk_off_w : vk_sw_extent.width;
        cp.extent.height = vk_off_h < vk_sw_extent.height ? vk_off_h : vk_sw_extent.height;
        cp.extent.depth = 1;
        vkCmdCopyImage(cmd, vk_off_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       sw_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
    }
    VkImageMemoryBarrier to_present = to_dst;
    to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_present.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0, 0, NULL, 0, NULL, 1, &to_present);
}

/* --- render pass --- */

static int ds_vk_create_renderpass(void) {
    VkAttachmentDescription att = { 0 };
    att.format = vk_sw_format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;      /* каждый кадр с чистого листа */
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; /* содержимое не нужно */
    att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; /* дальше blit */
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { 0 };
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    /* Кадр N+1 пишет оффскрин, пока кадр N ещё может читать его в blit
     * (два кадра в полёте): подпаиваемся под предыдущий transfer. */
    VkSubpassDependency dep = { 0 };
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ri = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    ri.attachmentCount = 1; ri.pAttachments = &att;
    ri.subpassCount = 1; ri.pSubpasses = &sub;
    ri.dependencyCount = 1; ri.pDependencies = &dep;
    return ds_vk_check_result(vkCreateRenderPass(vk_device, &ri, NULL, &vk_renderpass), "vkCreateRenderPass");
}

/* --- инициализация и выключение --- */

static void ds_vk_shutdown_backend(void);

static int ds_vk_init_backend(ANativeWindow *window) {
    if (vk_ready) return 1;
    vk_window = window;
    if (!window) { ds_log_err("vulkan: no window"); return 0; }

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "Cubic Battle";
    app.applicationVersion = 1;
    app.pEngineName = "DimScript Vulkan";
    app.engineVersion = 1;
    app.apiVersion = VK_API_VERSION_1_0;
    const char *inst_exts[] = { "VK_KHR_surface", "VK_KHR_android_surface" };
    VkInstanceCreateInfo ii = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ii.pApplicationInfo = &app;
    ii.enabledExtensionCount = 2;
    ii.ppEnabledExtensionNames = inst_exts;
    if (!ds_vk_check_result(vkCreateInstance(&ii, NULL, &vk_instance), "vkCreateInstance")) return 0;

    VkAndroidSurfaceCreateInfoKHR sci = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
    sci.window = window;
    if (!ds_vk_check_result(vkCreateAndroidSurfaceKHR(vk_instance, &sci, NULL, &vk_surface),
                            "vkCreateAndroidSurfaceKHR")) { ds_vk_shutdown_backend(); return 0; }

    uint32_t ndev = 0;
    if (!ds_vk_check_result(vkEnumeratePhysicalDevices(vk_instance, &ndev, NULL),
                            "vkEnumeratePhysicalDevices") || ndev == 0) {
        ds_log_err("vulkan: no physical devices");
        ds_vk_shutdown_backend(); return 0;
    }
    VkPhysicalDevice *devs = (VkPhysicalDevice *)malloc(ndev * sizeof(*devs));
    if (!devs) { ds_vk_shutdown_backend(); return 0; }
    if (!ds_vk_check_result(vkEnumeratePhysicalDevices(vk_instance, &ndev, devs),
                            "vkEnumeratePhysicalDevices")) { free(devs); ds_vk_shutdown_backend(); return 0; }
    vk_phys = VK_NULL_HANDLE;
    for (uint32_t d = 0; d < ndev && vk_phys == VK_NULL_HANDLE; d++) {
        uint32_t nf = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &nf, NULL);
        if (nf == 0) continue;
        VkQueueFamilyProperties *fams = (VkQueueFamilyProperties *)malloc(nf * sizeof(*fams));
        if (!fams) break;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[d], &nf, fams);
        for (uint32_t q = 0; q < nf; q++) {
            if (!(fams[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(devs[d], q, vk_surface, &present);
            if (present) {
                vk_phys = devs[d];
                vk_queue_family = q;
                break;
            }
        }
        free(fams);
    }
    free(devs);
    if (vk_phys == VK_NULL_HANDLE) {
        ds_log_err("vulkan: no graphics+present queue family");
        ds_vk_shutdown_backend(); return 0;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(vk_phys, &props);
    ds_log("vulkan: device '%s' (api %u.%u)", props.deviceName,
           VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion));

    const char *dev_exts[] = { "VK_KHR_swapchain" };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qi.queueFamilyIndex = vk_queue_family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo di = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = dev_exts;
    if (!ds_vk_check_result(vkCreateDevice(vk_phys, &di, NULL, &vk_device), "vkCreateDevice")) {
        ds_vk_shutdown_backend(); return 0;
    }
    vkGetDeviceQueue(vk_device, vk_queue_family, 0, &vk_queue);

    /* Пул разовых загрузок. */
    VkCommandPoolCreateInfo upi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    upi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    upi.queueFamilyIndex = vk_queue_family;
    if (!ds_vk_check_result(vkCreateCommandPool(vk_device, &upi, NULL, &vk_upload_pool), "vkCreateCommandPool(upload)")) {
        ds_vk_shutdown_backend(); return 0;
    }
    VkCommandBufferAllocateInfo uai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    uai.commandPool = vk_upload_pool;
    uai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    uai.commandBufferCount = 1;
    if (!ds_vk_check_result(vkAllocateCommandBuffers(vk_device, &uai, &vk_upload_cmd), "vkAllocateCommandBuffers(upload)")) {
        ds_vk_shutdown_backend(); return 0;
    }
    VkFenceCreateInfo ufi = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    ufi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (!ds_vk_check_result(vkCreateFence(vk_device, &ufi, NULL, &vk_upload_fence), "vkCreateFence(upload)")) {
        ds_vk_shutdown_backend(); return 0;
    }

    /* Раскладка дескрипторов и сэмплер. */
    VkDescriptorSetLayoutBinding lb = { 0 };
    lb.binding = 0;
    lb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    lb.descriptorCount = 1;
    lb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    dli.bindingCount = 1;
    dli.pBindings = &lb;
    if (!ds_vk_check_result(vkCreateDescriptorSetLayout(vk_device, &dli, NULL, &vk_desc_layout),
                            "vkCreateDescriptorSetLayout")) { ds_vk_shutdown_backend(); return 0; }
    VkSamplerCreateInfo smi = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    smi.magFilter = VK_FILTER_NEAREST; /* как попиксельный софтрендер */
    smi.minFilter = VK_FILTER_NEAREST;
    smi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    smi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    smi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    smi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (!ds_vk_check_result(vkCreateSampler(vk_device, &smi, NULL, &vk_sampler), "vkCreateSampler")) {
        ds_vk_shutdown_backend(); return 0;
    }

    /* Swapchain нужен до render pass (формат оффскрина = формат swapchain). */
    if (!ds_vk_recreate_swapchain()) { ds_vk_shutdown_backend(); return 0; }
    if (!ds_vk_create_renderpass()) { ds_vk_shutdown_backend(); return 0; }

    /* Раскладки конвейеров: push-константа с масштабом экрана; у текстурных
     * ещё и дескриптор сэмплера во фрагментном шейдере. */
    VkPushConstantRange pcr = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 4 };
    VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (!ds_vk_check_result(vkCreatePipelineLayout(vk_device, &pli, NULL, &vk_layout_solid),
                            "vkCreatePipelineLayout(solid)")) { ds_vk_shutdown_backend(); return 0; }
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &vk_desc_layout;
    if (!ds_vk_check_result(vkCreatePipelineLayout(vk_device, &pli, NULL, &vk_layout_image),
                            "vkCreatePipelineLayout(image)")) { ds_vk_shutdown_backend(); return 0; }
    if (!ds_vk_check_result(vkCreatePipelineLayout(vk_device, &pli, NULL, &vk_layout_tint),
                            "vkCreatePipelineLayout(tint)")) { ds_vk_shutdown_backend(); return 0; }

    VkShaderModule frag_solid = VK_NULL_HANDLE, frag_image = VK_NULL_HANDLE, frag_tint = VK_NULL_HANDLE;
    if (!ds_vk_create_shader_module(ds_spirv_solid_frag,
                                    sizeof(ds_spirv_solid_frag) / sizeof(uint32_t), &frag_solid) ||
        !ds_vk_create_shader_module(ds_spirv_image_frag,
                                    sizeof(ds_spirv_image_frag) / sizeof(uint32_t), &frag_image) ||
        !ds_vk_create_shader_module(ds_spirv_tint_frag,
                                    sizeof(ds_spirv_tint_frag) / sizeof(uint32_t), &frag_tint) ||
        !ds_vk_create_pipeline(frag_solid, vk_layout_solid, 1, &vk_pipe_solid) ||
        !ds_vk_create_pipeline(frag_image, vk_layout_image, 1, &vk_pipe_image) ||
        !ds_vk_create_pipeline(frag_tint, vk_layout_tint, 1, &vk_pipe_tint) ||
        !ds_vk_create_pipeline(frag_solid, vk_layout_solid, 0, &vk_pipe_replace)) {
        if (frag_solid != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_solid, NULL);
        if (frag_image != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_image, NULL);
        if (frag_tint != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_tint, NULL);
        ds_vk_shutdown_backend(); return 0;
    }

    if (!ds_vk_create_frame_resources()) { ds_vk_shutdown_backend(); return 0; }

    vk_ready = 1;
    ds_log("vulkan: renderer ready");
    return 1;
}

static void ds_vk_shutdown_backend(void) {
    ds_vk_pending_reset();
    if (vk_device) {
        vkDeviceWaitIdle(vk_device);
        /* Сначала swapchain: он попутно утилизирует семафоры захвата кадров. */
        ds_vk_destroy_swapchain();
        ds_vk_destroy_frame_resources();
        ds_vk_destroy_offscreen();
        if (vk_pipe_solid != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_solid, NULL);
        if (vk_pipe_image != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_image, NULL);
        if (vk_pipe_tint != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_tint, NULL);
        if (vk_pipe_replace != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_replace, NULL);
        vk_pipe_solid = vk_pipe_image = vk_pipe_tint = VK_NULL_HANDLE;
        vk_pipe_replace = VK_NULL_HANDLE;
        if (vk_layout_solid != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk_device, vk_layout_solid, NULL);
        if (vk_layout_image != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk_device, vk_layout_image, NULL);
        if (vk_layout_tint != VK_NULL_HANDLE) vkDestroyPipelineLayout(vk_device, vk_layout_tint, NULL);
        vk_layout_solid = vk_layout_image = vk_layout_tint = VK_NULL_HANDLE;
        if (vk_renderpass != VK_NULL_HANDLE) vkDestroyRenderPass(vk_device, vk_renderpass, NULL);
        vk_renderpass = VK_NULL_HANDLE;
        if (vk_sampler != VK_NULL_HANDLE) vkDestroySampler(vk_device, vk_sampler, NULL);
        vk_sampler = VK_NULL_HANDLE;
        if (vk_desc_layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(vk_device, vk_desc_layout, NULL);
        vk_desc_layout = VK_NULL_HANDLE;
        for (size_t i = 0; i < vk_desc_pool_n; i++)
            if (vk_desc_pools[i] != VK_NULL_HANDLE) vkDestroyDescriptorPool(vk_device, vk_desc_pools[i], NULL);
        free(vk_desc_pools);
        vk_desc_pools = NULL; vk_desc_pool_n = vk_desc_pool_cap = vk_desc_pool_used = 0;
        ds_vk_font_gpu_release();
        /* GPU-копии текстур умирают вместе с устройством; CPU-пиксели остаются,
         * чтобы перезалить их при повторной инициализации окна. */
        for (Texture *t = textures; t; t = t->next) ds_vk_texture_gpu_release(t);
        if (vk_upload_fence != VK_NULL_HANDLE) vkDestroyFence(vk_device, vk_upload_fence, NULL);
        if (vk_upload_pool != VK_NULL_HANDLE) vkDestroyCommandPool(vk_device, vk_upload_pool, NULL);
        vk_upload_fence = VK_NULL_HANDLE; vk_upload_pool = VK_NULL_HANDLE; vk_upload_cmd = VK_NULL_HANDLE;
        vkDestroyDevice(vk_device, NULL);
        vk_device = VK_NULL_HANDLE;
    }
    if (vk_surface != VK_NULL_HANDLE && vk_instance != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(vk_instance, vk_surface, NULL);
    vk_surface = VK_NULL_HANDLE;
    if (vk_instance != VK_NULL_HANDLE) vkDestroyInstance(vk_instance, NULL);
    vk_instance = VK_NULL_HANDLE;
    vk_phys = VK_NULL_HANDLE;
    vk_queue = VK_NULL_HANDLE;
    vk_window = NULL;
    vk_ready = 0;
    vk_need_recreate = 0;
    vk_image_held = 0;
    free(vk_batches);
    vk_batches = NULL; vk_batch_n = vk_batch_cap = 0;
}

/* --- покадровый цикл --- */

static int ds_vk_acquire_image(void) {
    DsVkFrame *f = &vk_frames[vk_frame_cur];
    VkResult r = vkAcquireNextImageKHR(vk_device, vk_swap, UINT64_MAX, f->image_available,
                                       VK_NULL_HANDLE, &vk_sw_index);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_NOT_READY || r == VK_TIMEOUT) {
        /* OUT_OF_DATE - пересоздаём; остальное может дать "протухший" swapchain
         * после recreate окна: тоже пробуем один раз пересоздать. */
        if (!ds_vk_recreate_swapchain()) return 0;
        r = vkAcquireNextImageKHR(vk_device, vk_swap, UINT64_MAX, f->image_available,
                                  VK_NULL_HANDLE, &vk_sw_index);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        if (r != VK_ERROR_OUT_OF_DATE_KHR)
            ds_log_err("vulkan: vkAcquireNextImageKHR failed (%d)", (int)r);
        return 0;
    }
    if (r == VK_SUBOPTIMAL_KHR) vk_need_recreate = 1;
    vk_image_held = 1;
    return 1;
}

static int ds_vk_begin_frame_backend(uint32_t w, uint32_t h) {
    if (!vk_ready) return 0;
    if (w == 0 || h == 0) return 0;
    /* Размер окна мог смениться (поворот, IME): сверяемся с поверхностью. */
    if (vk_swap == VK_NULL_HANDLE || vk_need_recreate) {
        if (!ds_vk_recreate_swapchain()) return 0;
    } else {
        VkSurfaceCapabilitiesKHR caps;
        if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk_phys, vk_surface, &caps) == VK_SUCCESS) {
            if (caps.currentExtent.width != vk_sw_extent.width ||
                caps.currentExtent.height != vk_sw_extent.height) {
                if (!ds_vk_recreate_swapchain()) return 0;
            }
        }
    }
    if (!ds_vk_ensure_offscreen(w, h, vk_sw_format)) return 0;

    /* Слот кадра: ждём его предыдущий submit (если был), затем пишем новый. */
    vk_frame_cur = (vk_frame_cur + 1) % DS_VK_FRAMES_IN_FLIGHT;
    DsVkFrame *f = &vk_frames[vk_frame_cur];
    if (f->submitted) {
        if (!ds_vk_check_result(vkWaitForFences(vk_device, 1, &f->fence, VK_TRUE, UINT64_MAX),
                                "vkWaitForFences(frame)")) return 0;
        f->submitted = 0;
    }
    if (!ds_vk_check_result(vkResetCommandBuffer(f->cmd, 0), "vkResetCommandBuffer(frame)")) return 0;

    if (!vk_image_held && !ds_vk_acquire_image()) return 0;

    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (!ds_vk_check_result(vkBeginCommandBuffer(f->cmd, &bi), "vkBeginCommandBuffer(frame)")) return 0;
    VkClearValue clear;
    clear.color = (VkClearColorValue) { { 0.0f, 0.0f, 0.0f, 0.0f } };
    VkRenderPassBeginInfo ri = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    ri.renderPass = vk_renderpass;
    ri.framebuffer = vk_off_fb;
    ri.renderArea.extent.width = w;
    ri.renderArea.extent.height = h;
    ri.clearValueCount = 1;
    ri.pClearValues = &clear;
    vkCmdBeginRenderPass(f->cmd, &ri, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { w, h } };
    vkCmdSetViewport(f->cmd, 0, 1, &vp);
    vkCmdSetScissor(f->cmd, 0, 1, &sc);
    return 1;
}


static void ds_vk_end_frame_backend(void) {
    DsVkFrame *f = &vk_frames[vk_frame_cur];
    ds_vk_process_pending_uploads();
    if (ds_vk_build_geometry() && geo_in > 0 &&
        ds_vk_frame_ensure_capacity(f, geo_vn, geo_in)) {
        memcpy(f->vmap, geo_verts, geo_vn * sizeof(GeoVert));
        memcpy(f->imap, geo_idx, geo_in * sizeof(uint16_t));
        /* Привязка здесь, а не в begin: ёмкость буферов могла вырасти, и это
         * уже новые объекты - биндим финальные. */
        VkDeviceSize zero = 0;
        vkCmdBindVertexBuffers(f->cmd, 0, 1, &f->vbuf, &zero);
        vkCmdBindIndexBuffer(f->cmd, f->ibuf, 0, VK_INDEX_TYPE_UINT16);
        ds_vk_record_draws(f->cmd);
    }
    vkCmdEndRenderPass(f->cmd);
    if (vk_swap == VK_NULL_HANDLE || vk_sw_index >= vk_sw_count || vk_off_image == VK_NULL_HANDLE)
        return; /* показывать нечего; кадр остаётся захваченным - переиспользуем */
    ds_vk_record_present_blit(f->cmd);
    if (!ds_vk_check_result(vkEndCommandBuffer(f->cmd), "vkEndCommandBuffer(frame)"))
        return;
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &f->image_available;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f->cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &vk_sw_present[vk_sw_index];
    /* Фенс сбрасываем ровно перед submit: к этому моменту его предыдущий
     * submit уже дождался в begin_frame (см. флаг submitted). */
    if (!ds_vk_check_result(vkResetFences(vk_device, 1, &f->fence), "vkResetFences(frame)"))
        return;
    if (!ds_vk_check_result(vkQueueSubmit(vk_queue, 1, &si, f->fence), "vkQueueSubmit(frame)"))
        return; /* фенс остался сброшенным; submitted=0 - ждать его не будем */
    f->submitted = 1;
    VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &vk_sw_present[vk_sw_index];
    pi.swapchainCount = 1;
    pi.pSwapchains = &vk_swap;
    pi.pImageIndices = &vk_sw_index;
    VkResult pr = vkQueuePresentKHR(vk_queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) vk_need_recreate = 1;
    else if (pr != VK_SUCCESS) ds_log_err("vulkan: vkQueuePresentKHR failed (%d)", (int)pr);
    vk_image_held = 0;
}
