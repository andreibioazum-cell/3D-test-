/* Хост-регрессионный тест Vulkan-инициализации (native/graphics/vulkan_backend.inc).
 *
 * Воспроизводит класс краша с поля (TECNO KL4 / Mali-G57, tombstone:
 * «null pointer dereference» внутри драйвера GPU при создании конвейеров в
 * ds_graphics_init). Причина была в том, что VkPipelineShaderStageCreateInfo
 * создавался без нулевой инициализации: в pNext/flags оставался мусор стека,
 * а загрузчик Vulkan и драйвер обходят цепочку pNext каждого create-info —
 * мусорный указатель = разыменование несуществующего адреса прямо в драйвере.
 *
 * Тест запускает ds_graphics_init в потоке, чей стек ЗАРАНЕЕ заполнен 0xDE:
 * любое поле структуры, которое код не инициализировал, становится
 * ненулевым мусором (как «грязный» стек на телефоне), и строгий фейковый
 * драйвер (проверяет pNext/flags, как настоящий) ловит нарушение вместо
 * того, чтобы падать с SIGSEGV. Потоком же проверяются два полных кадра и
 * пересоздание swapchain со СМЕНОЙ ФОРМАТА (поворот) - путь пересборки
 * render pass/конвейеров (vk_rp_format) в ds_vk_begin_frame_backend.
 *
 * Фейковый драйвер моделирует реалистичный Android: окно в ландшафте на
 * портретном дисплее (currentTransform = ROTATE_90). Поэтому же проверяется
 * preTransform swapchain: игра рисует в координатах окна, и правильный
 * preTransform — IDENTITY (поворот окна к дисплею делает система).
 * preTransform = currentTransform — классическая ошибка, при которой в
 * ландшафте вся картинка оказывается повёрнутой на 90° и растянутой.
 *
 * Сборка и запуск (из корня репозитория; нужны Vulkan-заголовки,
 * например клон KhronosGroup/Vulkan-Headers):
 *   gcc -std=gnu99 -O1 -o /tmp/test_vk_init \
 *       tools/host_test/test_vk_init.c \
 *       -I tools/host_test/stub -I /tmp/vktools/Vulkan-Headers/include -I . -lm -lpthread
 *   /tmp/test_vk_init
 */
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <pthread.h>

/* --- строгий фейковый драйвер: валидирует вход как настоящий --- */

static int g_violations = 0;
static char g_violation_msg[256];
static int g_pipeline_creates = 0;
static int g_swapchain_creates = 0;
static int g_present_suboptimal_once = 1;
static uint64_t g_next_handle = 0x1000;

static void g_violation(const char *fmt, ...) {
    if (g_violations) return; /* первое сообщение важнее всего */
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_violation_msg, sizeof g_violation_msg, fmt, ap);
    va_end(ap);
    g_violations++;
}
static void *g_next(void) { return (void *)(g_next_handle++); }

/* Проверяем всё, что настоящий драйвер обязан проверить в
 * vkCreateGraphicsPipelines, в первую очередь pNext/flags: мусор в них —
 * ровно тот баг, который ронял Mali. */
static int g_validate_pipeline_create(const VkGraphicsPipelineCreateInfo *pi) {
    const char *bad = NULL;
    if (pi->sType != VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO) bad = "createInfo.sType";
    else if (pi->pNext) bad = "createInfo.pNext != NULL (мусор стека!)";
    else if (pi->flags) bad = "createInfo.flags != 0 (мусор стека!)";
    else if (pi->stageCount < 1 || !pi->pStages) bad = "createInfo.pStages";
    else if (!pi->layout) bad = "createInfo.layout == NULL";
    else if (!pi->renderPass) bad = "createInfo.renderPass == NULL";
    else if (pi->subpass != 0) bad = "createInfo.subpass != 0";
    for (uint32_t i = 0; i < pi->stageCount && !bad; i++) {
        const VkPipelineShaderStageCreateInfo *st = &pi->pStages[i];
        if (st->sType != VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO) bad = "stage.sType";
        else if (st->pNext) bad = "stage.pNext != NULL (мусор стека!)";
        else if (st->flags) bad = "stage.flags != 0 (мусор стека!)";
        else if (st->module == VK_NULL_HANDLE) bad = "stage.module == NULL";
        else if (!st->pName) bad = "stage.pName == NULL";
    }
    struct { const void *p; const char *name; VkStructureType type; } subs[8];
    int n = 0;
    if (pi->pVertexInputState) { subs[n].p = pi->pVertexInputState; subs[n].name = "vertexInput"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO; n++; }
    if (pi->pInputAssemblyState) { subs[n].p = pi->pInputAssemblyState; subs[n].name = "inputAssembly"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO; n++; }
    if (pi->pViewportState) { subs[n].p = pi->pViewportState; subs[n].name = "viewport"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO; n++; }
    if (pi->pRasterizationState) { subs[n].p = pi->pRasterizationState; subs[n].name = "rasterization"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO; n++; }
    if (pi->pMultisampleState) { subs[n].p = pi->pMultisampleState; subs[n].name = "multisample"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO; n++; }
    if (pi->pDepthStencilState) { subs[n].p = pi->pDepthStencilState; subs[n].name = "depthStencil"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; n++; }
    if (pi->pColorBlendState) { subs[n].p = pi->pColorBlendState; subs[n].name = "colorBlend"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO; n++; }
    if (pi->pDynamicState) { subs[n].p = pi->pDynamicState; subs[n].name = "dynamicState"; subs[n].type = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO; n++; }
    for (int i = 0; i < n && !bad; i++) {
        const VkBaseInStructure *b = (const VkBaseInStructure *)subs[i].p;
        if (b->sType != subs[i].type) bad = subs[i].name;
        else if (b->pNext) bad = subs[i].name;
    }
    if (bad) { g_violation("vkCreateGraphicsPipelines: %s", bad); return 0; }
    return 1;
}

static void g_fill_format_props(VkFormatProperties *fp) {
    memset(fp, 0, sizeof *fp);
    fp->optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
}

/* --- фейковые реализации (только то, что использует графический TU) --- */

VkResult vkCreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *ac, VkInstance *out) {
    (void)ac;
    if (ci->pNext) { g_violation("vkCreateInstance: pNext != NULL"); return VK_ERROR_INITIALIZATION_FAILED; }
    *out = (VkInstance)g_next();
    return VK_SUCCESS;
}
void vkDestroyInstance(VkInstance i, const VkAllocationCallbacks *ac) { (void)i; (void)ac; }
VkResult vkEnumeratePhysicalDevices(VkInstance i, uint32_t *count, VkPhysicalDevice *devs) {
    (void)i;
    static VkPhysicalDevice dev = (VkPhysicalDevice)0x5001;
    if (!devs) { *count = 1; return VK_SUCCESS; }
    devs[0] = dev; *count = 1;
    return VK_SUCCESS;
}
void vkGetPhysicalDeviceProperties(VkPhysicalDevice d, VkPhysicalDeviceProperties *p) {
    (void)d;
    memset(p, 0, sizeof *p);
    p->apiVersion = VK_API_VERSION_1_1;
    p->deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    strcpy(p->deviceName, "FakeMali-G57");
}
void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice d, uint32_t *count, VkQueueFamilyProperties *fams) {
    (void)d;
    if (!fams) { *count = 1; return; }
    memset(fams, 0, sizeof *fams);
    fams->queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    fams->timestampValidBits = 64;
    fams->minImageTransferGranularity.width = 1;
    fams->minImageTransferGranularity.height = 1;
    *count = 1;
}
VkResult vkCreateDevice(VkPhysicalDevice d, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *ac, VkDevice *out) {
    (void)d; (void)ac;
    if (ci->pNext) { g_violation("vkCreateDevice: pNext != NULL"); return VK_ERROR_INITIALIZATION_FAILED; }
    *out = (VkDevice)g_next();
    return VK_SUCCESS;
}
void vkGetDeviceQueue(VkDevice d, uint32_t fam, uint32_t idx, VkQueue *q) { (void)d; (void)idx; *q = (VkQueue)(uintptr_t)(0x6000 + fam); }
void vkDestroyDevice(VkDevice d, const VkAllocationCallbacks *ac) { (void)d; (void)ac; }
VkResult vkDeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }
VkResult vkQueueWaitIdle(VkQueue q) { (void)q; return VK_SUCCESS; }
VkResult vkQueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *si, VkFence f) { (void)q; (void)n; (void)si; (void)f; return VK_SUCCESS; }
VkResult vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    (void)q; (void)pi;
    if (g_present_suboptimal_once) { g_present_suboptimal_once = 0; return VK_SUBOPTIMAL_KHR; }
    return VK_SUCCESS;
}
VkResult vkCreateSwapchainKHR(VkDevice d, const VkSwapchainCreateInfoKHR *ci, const VkAllocationCallbacks *ac, VkSwapchainKHR *out) {
    (void)d; (void)ac;
    if (ci->imageExtent.width == 0 || ci->imageExtent.height == 0) { g_violation("swapchain: пустой extent"); return VK_ERROR_INITIALIZATION_FAILED; }
    /* Android: буфер живёт в системе координат ОКНА, поворот окна к дисплею
     * делает системный композитор. Поэтому единственно правильный preTransform
     * для игры, рисующей в координатах окна, — IDENTITY. currentTransform
     * (ROTATE_90 в ландшафте) проворачивает картинку на 90° ещё раз. */
    if (ci->preTransform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
        g_violation("swapchain: preTransform != IDENTITY (картинка будет повёрнута на 90° в ландшафте)");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    g_swapchain_creates++;
    *out = (VkSwapchainKHR)g_next();
    return VK_SUCCESS;
}
void vkDestroySwapchainKHR(VkDevice d, VkSwapchainKHR s, const VkAllocationCallbacks *ac) { (void)d; (void)s; (void)ac; }
VkResult vkGetSwapchainImagesKHR(VkDevice d, VkSwapchainKHR s, uint32_t *count, VkImage *imgs) {
    (void)d; (void)s;
    if (!imgs) { *count = 3; return VK_SUCCESS; }
    for (uint32_t i = 0; i < *count && i < 3; i++) imgs[i] = (VkImage)g_next();
    *count = 3;
    return VK_SUCCESS;
}
VkResult vkAcquireNextImageKHR(VkDevice d, VkSwapchainKHR s, uint64_t t, VkSemaphore sem, VkFence f, uint32_t *idx) {
    (void)d; (void)s; (void)t; (void)sem; (void)f;
    *idx = 0;
    return VK_SUCCESS;
}
VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice d, VkSurfaceKHR s, VkSurfaceCapabilitiesKHR *c) {
    (void)d; (void)s;
    memset(c, 0, sizeof *c);
    c->currentExtent.width = 720; c->currentExtent.height = 1280;
    c->minImageCount = 2; c->maxImageCount = 0; c->maxImageArrayLayers = 1;
    /* Реалистичный Android: портретный дисплей, окно в ландшафте —
     * система поворачивает окно на 90° (currentTransform = ROTATE_90). */
    c->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    c->currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    c->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    return VK_SUCCESS;
}
VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice d, VkSurfaceKHR s, uint32_t *count, VkSurfaceFormatKHR *fmts) {
    (void)d; (void)s;
    /* Первый swapchain - B8G8R8A8, пересоздание («поворот») - R8G8B8A8:
     * проверяет пересборку render pass/конвейеров при смене формата. */
    VkFormat f = (g_swapchain_creates == 0) ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
    if (!fmts) { *count = 1; return VK_SUCCESS; }
    fmts[0].format = f;
    fmts[0].colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    *count = 1;
    return VK_SUCCESS;
}
VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice d, uint32_t q, VkSurfaceKHR s, VkBool32 *ok) {
    (void)d; (void)q; (void)s;
    *ok = VK_TRUE;
    return VK_SUCCESS;
}
void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice d, VkFormat f, VkFormatProperties *fp) { (void)d; (void)f; g_fill_format_props(fp); }
VkResult vkCreateAndroidSurfaceKHR(VkInstance i, const VkAndroidSurfaceCreateInfoKHR *ci, const VkAllocationCallbacks *ac, VkSurfaceKHR *out) {
    (void)i; (void)ac;
    if (!ci->window) { g_violation("android surface: window == NULL"); return VK_ERROR_INITIALIZATION_FAILED; }
    *out = (VkSurfaceKHR)g_next();
    return VK_SUCCESS;
}
void vkDestroySurfaceKHR(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks *ac) { (void)i; (void)s; (void)ac; }
void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice d, VkPhysicalDeviceMemoryProperties *mp) {
    (void)d;
    memset(mp, 0, sizeof *mp);
    mp->memoryTypeCount = 2;
    mp->memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    mp->memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    mp->memoryHeapCount = 1;
    mp->memoryHeaps[0].size = 0x10000000;
    mp->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
}
VkResult vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *ai, const VkAllocationCallbacks *ac, VkDeviceMemory *out) {
    (void)d; (void)ai; (void)ac;
    *out = (VkDeviceMemory)g_next();
    return VK_SUCCESS;
}
void vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *ac) { (void)d; (void)m; (void)ac; }
VkResult vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize off, VkDeviceSize size, VkMemoryMapFlags fl, void **pp) {
    (void)d; (void)m; (void)off; (void)fl;
    *pp = malloc(size ? size : 1);
    return *pp ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}
void vkUnmapMemory(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; }
VkResult vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *ci, const VkAllocationCallbacks *ac, VkBuffer *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkBuffer)g_next();
    return VK_SUCCESS;
}
void vkDestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *ac) { (void)d; (void)b; (void)ac; }
void vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *mr) {
    (void)d; (void)b;
    memset(mr, 0, sizeof *mr);
    mr->size = 4096; mr->alignment = 256; mr->memoryTypeBits = 0x3;
}
VkResult vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize off) { (void)d; (void)b; (void)m; (void)off; return VK_SUCCESS; }
VkResult vkCreateImage(VkDevice d, const VkImageCreateInfo *ci, const VkAllocationCallbacks *ac, VkImage *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkImage)g_next();
    return VK_SUCCESS;
}
void vkDestroyImage(VkDevice d, VkImage i, const VkAllocationCallbacks *ac) { (void)d; (void)i; (void)ac; }
void vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *mr) {
    (void)d; (void)i;
    memset(mr, 0, sizeof *mr);
    mr->size = 4096; mr->alignment = 256; mr->memoryTypeBits = 0x1;
}
VkResult vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize off) { (void)d; (void)i; (void)m; (void)off; return VK_SUCCESS; }
VkResult vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *ci, const VkAllocationCallbacks *ac, VkImageView *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkImageView)g_next();
    return VK_SUCCESS;
}
void vkDestroyImageView(VkDevice d, VkImageView v, const VkAllocationCallbacks *ac) { (void)d; (void)v; (void)ac; }
VkResult vkCreateRenderPass(VkDevice d, const VkRenderPassCreateInfo *ci, const VkAllocationCallbacks *ac, VkRenderPass *out) {
    (void)d; (void)ac;
    if (ci->pNext) { g_violation("vkCreateRenderPass: pNext != NULL"); return VK_ERROR_INITIALIZATION_FAILED; }
    *out = (VkRenderPass)g_next();
    return VK_SUCCESS;
}
void vkDestroyRenderPass(VkDevice d, VkRenderPass rp, const VkAllocationCallbacks *ac) { (void)d; (void)rp; (void)ac; }
VkResult vkCreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo *ci, const VkAllocationCallbacks *ac, VkFramebuffer *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkFramebuffer)g_next();
    return VK_SUCCESS;
}
void vkDestroyFramebuffer(VkDevice d, VkFramebuffer fb, const VkAllocationCallbacks *ac) { (void)d; (void)fb; (void)ac; }
VkResult vkCreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *ci, const VkAllocationCallbacks *ac, VkPipelineLayout *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkPipelineLayout)g_next();
    return VK_SUCCESS;
}
void vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout pl, const VkAllocationCallbacks *ac) { (void)d; (void)pl; (void)ac; }
VkResult vkCreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *ci, const VkAllocationCallbacks *ac, VkShaderModule *out) {
    (void)d; (void)ac;
    if (ci->pNext) { g_violation("vkCreateShaderModule: pNext != NULL"); return VK_ERROR_INITIALIZATION_FAILED; }
    *out = (VkShaderModule)g_next();
    return VK_SUCCESS;
}
void vkDestroyShaderModule(VkDevice d, VkShaderModule sm, const VkAllocationCallbacks *ac) { (void)d; (void)sm; (void)ac; }
VkResult vkCreateGraphicsPipelines(VkDevice d, VkPipelineCache pc, uint32_t n, const VkGraphicsPipelineCreateInfo *cis, const VkAllocationCallbacks *ac, VkPipeline *out) {
    (void)d; (void)pc; (void)n; (void)ac;
    g_pipeline_creates++;
    if (!g_validate_pipeline_create(cis)) return VK_ERROR_INITIALIZATION_FAILED;
    *out = (VkPipeline)g_next();
    return VK_SUCCESS;
}
void vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *ac) { (void)d; (void)p; (void)ac; }
VkResult vkCreateSampler(VkDevice d, const VkSamplerCreateInfo *ci, const VkAllocationCallbacks *ac, VkSampler *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkSampler)g_next();
    return VK_SUCCESS;
}
void vkDestroySampler(VkDevice d, VkSampler s, const VkAllocationCallbacks *ac) { (void)d; (void)s; (void)ac; }
VkResult vkCreateDescriptorSetLayout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *ci, const VkAllocationCallbacks *ac, VkDescriptorSetLayout *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkDescriptorSetLayout)g_next();
    return VK_SUCCESS;
}
void vkDestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout l, const VkAllocationCallbacks *ac) { (void)d; (void)l; (void)ac; }
VkResult vkCreateDescriptorPool(VkDevice d, const VkDescriptorPoolCreateInfo *ci, const VkAllocationCallbacks *ac, VkDescriptorPool *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkDescriptorPool)g_next();
    return VK_SUCCESS;
}
void vkDestroyDescriptorPool(VkDevice d, VkDescriptorPool p, const VkAllocationCallbacks *ac) { (void)d; (void)p; (void)ac; }
VkResult vkAllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo *ai, VkDescriptorSet *sets) {
    (void)d; (void)ai;
    *sets = (VkDescriptorSet)g_next();
    return VK_SUCCESS;
}
void vkUpdateDescriptorSets(VkDevice d, uint32_t nw, const VkWriteDescriptorSet *w, uint32_t nc, const VkCopyDescriptorSet *c) {
    (void)d; (void)nw; (void)w; (void)nc; (void)c;
}
VkResult vkCreateCommandPool(VkDevice d, const VkCommandPoolCreateInfo *ci, const VkAllocationCallbacks *ac, VkCommandPool *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkCommandPool)g_next();
    return VK_SUCCESS;
}
void vkDestroyCommandPool(VkDevice d, VkCommandPool p, const VkAllocationCallbacks *ac) { (void)d; (void)p; (void)ac; }
VkResult vkAllocateCommandBuffers(VkDevice d, const VkCommandBufferAllocateInfo *ai, VkCommandBuffer *cbs) {
    (void)d; (void)ai;
    *cbs = (VkCommandBuffer)g_next();
    return VK_SUCCESS;
}
VkResult vkResetCommandBuffer(VkCommandBuffer cb, VkCommandBufferResetFlags f) { (void)cb; (void)f; return VK_SUCCESS; }
VkResult vkBeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo *bi) { (void)cb; (void)bi; return VK_SUCCESS; }
VkResult vkEndCommandBuffer(VkCommandBuffer cb) { (void)cb; return VK_SUCCESS; }
VkResult vkCreateFence(VkDevice d, const VkFenceCreateInfo *ci, const VkAllocationCallbacks *ac, VkFence *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkFence)g_next();
    return VK_SUCCESS;
}
void vkDestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *ac) { (void)d; (void)f; (void)ac; }
VkResult vkResetFences(VkDevice d, uint32_t n, const VkFence *fs) { (void)d; (void)n; (void)fs; return VK_SUCCESS; }
VkResult vkWaitForFences(VkDevice d, uint32_t n, const VkFence *fs, VkBool32 all, uint64_t t) { (void)d; (void)n; (void)fs; (void)all; (void)t; return VK_SUCCESS; }
VkResult vkCreateSemaphore(VkDevice d, const VkSemaphoreCreateInfo *ci, const VkAllocationCallbacks *ac, VkSemaphore *out) {
    (void)d; (void)ci; (void)ac;
    *out = (VkSemaphore)g_next();
    return VK_SUCCESS;
}
void vkDestroySemaphore(VkDevice d, VkSemaphore s, const VkAllocationCallbacks *ac) { (void)d; (void)s; (void)ac; }

/* Команды буфера: на хосте некуда записывать. */
void vkCmdBindPipeline(VkCommandBuffer cb, VkPipelineBindPoint pb, VkPipeline p) { (void)cb; (void)pb; (void)p; }
/* Push-константы и области blit запоминаются: по ним проверяется апскейл —
 * шейдер обязан получать ЛОГИЧЕСКИЙ размер окна, а blit тянуть маленький
 * оффскрин на весь swapchain. */
static float g_pc[4];
static int g_pc_sets, g_blits, g_copies;
static int g_blit_src_w, g_blit_src_h, g_blit_dst_w, g_blit_dst_h;
void vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout l, VkShaderStageFlags s, uint32_t off, uint32_t size, const void *v) {
    (void)cb; (void)l; (void)s; (void)off;
    if (size >= sizeof g_pc && v) memcpy(g_pc, v, sizeof g_pc);
    g_pc_sets++;
}
void vkCmdBindDescriptorSets(VkCommandBuffer cb, VkPipelineBindPoint pb, VkPipelineLayout l, uint32_t first, uint32_t n, const VkDescriptorSet *sets, uint32_t din, const uint32_t *dyn) { (void)cb; (void)pb; (void)l; (void)first; (void)n; (void)sets; (void)din; (void)dyn; }
void vkCmdBindVertexBuffers(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkBuffer *bufs, const VkDeviceSize *offs) { (void)cb; (void)first; (void)n; (void)bufs; (void)offs; }
void vkCmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer b, VkDeviceSize off, VkIndexType t) { (void)cb; (void)b; (void)off; (void)t; }
void vkCmdDrawIndexed(VkCommandBuffer cb, uint32_t idx, uint32_t draws, uint32_t first, int32_t off, uint32_t firsti) { (void)cb; (void)idx; (void)draws; (void)first; (void)off; (void)firsti; }
void vkCmdBeginRenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo *bi, VkSubpassContents c) { (void)cb; (void)bi; (void)c; }
void vkCmdEndRenderPass(VkCommandBuffer cb) { (void)cb; }
void vkCmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkViewport *vps) { (void)cb; (void)first; (void)n; (void)vps; }
void vkCmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t n, const VkRect2D *rc) { (void)cb; (void)first; (void)n; (void)rc; }
void vkCmdPipelineBarrier(VkCommandBuffer cb, VkPipelineStageFlags s, VkPipelineStageFlags d, VkDependencyFlags fl, uint32_t nm, const VkMemoryBarrier *mb, uint32_t nb, const VkBufferMemoryBarrier *bb, uint32_t ni, const VkImageMemoryBarrier *ib) { (void)cb; (void)s; (void)d; (void)fl; (void)nm; (void)mb; (void)nb; (void)bb; (void)ni; (void)ib; }
void vkCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst, VkImageLayout dl, uint32_t n, const VkBufferImageCopy *rc) { (void)cb; (void)src; (void)dst; (void)dl; (void)n; (void)rc; }
void vkCmdBlitImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst, VkImageLayout dl, uint32_t n, const VkImageBlit *rc, VkFilter f) {
    (void)cb; (void)src; (void)sl; (void)dst; (void)dl;
    if (n == 1 && rc) {
        g_blit_src_w = rc[0].srcOffsets[1].x; g_blit_src_h = rc[0].srcOffsets[1].y;
        g_blit_dst_w = rc[0].dstOffsets[1].x; g_blit_dst_h = rc[0].dstOffsets[1].y;
        if (f != VK_FILTER_NEAREST) g_violation("blit: фильтр не NEAREST (апскейл обязан быть пиксельным)");
    }
    g_blits++;
}
void vkCmdCopyImage(VkCommandBuffer cb, VkImage src, VkImageLayout sl, VkImage dst, VkImageLayout dl, uint32_t n, const VkImageCopy *rc) { (void)cb; (void)src; (void)sl; (void)dst; (void)dl; (void)n; (void)rc; g_copies++; }

/* --- заглушки рантайма и Android (как в test_geometry.c) --- */

void ds_log(const char *format, ...) { (void)format; }
void ds_log_err(const char *format, ...) { (void)format; }
void ds_console_log(int is_error, const char *format, ...) { (void)is_error; (void)format; }
void ds_runtime_error(const char *format, ...) { (void)format; }
const char *ds_runtime_error_message(void) { return ""; }
int console_count(void) { return 0; }
const char *console_line(int i) { (void)i; return ""; }
int console_type(int i) { (void)i; return 0; }
int screen_w = 720, screen_h = 1280;

#include "graphics.c"

/* Android-заглушки: типы приходят из stub/android/asset_manager.h выше. */
AAsset *AAssetManager_open(AAssetManager *mgr, const char *name, int mode) { (void)mgr; (void)name; (void)mode; return NULL; }
off_t AAsset_getLength(AAsset *a) { (void)a; return 0; }
int AAsset_read(AAsset *a, void *buf, size_t n) { (void)a; (void)buf; (void)n; return -1; }
int AAsset_close(AAsset *a) { (void)a; return 0; }



/* --- сам тест --- */

#define TEST_STACK_SIZE (1u << 20)
static char test_stack[TEST_STACK_SIZE];
static struct { int init_ok; int frame1_ok; int frame2_ok; int frame3_ok;
                unsigned off_w, off_h, log_w, log_h; } g_res;

static void *test_thread(void *arg) {
    (void)arg;
    /* Весь стек потока уже заполнен 0xDE: незаинициализированные поля
     * структур в коде будут содержать мусор, а не ноль. */
    static int dummy_window;
    ANativeWindow *win = (ANativeWindow *)&dummy_window;
    g_res.init_ok = ds_graphics_init(NULL, win);
    if (!g_res.init_ok) return 0;
    Buffer b;
    memset(&b, 0, sizeof b);
    b.width = 720; b.height = 1280; b.stride = 720;
    g_res.frame1_ok = ds_graphics_begin_frame(&b);
    rect(0, 0, 10, 10, 0xff123456);
    ds_graphics_end_frame();
    /* Второй кадр: фейковый драйвер вернул SUBOPTIMAL на present, а при
     * пересоздании swapchain - другой формат (симуляция поворота).
     * Проверяет пересборку render pass/конвейеров (vk_rp_format). */
    g_res.frame2_ok = ds_graphics_begin_frame(&b);
    rect(0, 0, 10, 10, 0xff654321);
    ds_graphics_end_frame();
    /* Третий кадр: апскейла в настройках больше нет, поэтому оффскрин всегда
     * ровно с окно, логический размер совпадает с ним, а blit идёт 1:1. */
    g_res.frame3_ok = ds_graphics_begin_frame(&b);
    rect(0, 0, 10, 10, 0xff123456);
    ds_graphics_end_frame();
    g_res.off_w = vk_off_w; g_res.off_h = vk_off_h;
    g_res.log_w = vk_log_w; g_res.log_h = vk_log_h;
    ds_graphics_shutdown();
    return 0;
}

int main(void) {
    memset(test_stack, 0xDE, sizeof test_stack);
    pthread_t th;
    pthread_attr_t attr;
    int fail = 0;
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, test_stack, sizeof test_stack);
    pthread_create(&th, &attr, test_thread, NULL);
    pthread_join(th, NULL);
    pthread_attr_destroy(&attr);

    if (!g_res.init_ok) { fail = 1; printf("FAIL: ds_graphics_init вернул 0\n"); }
    if (!g_res.frame1_ok) { fail = 1; printf("FAIL: begin_frame первого кадра вернул 0\n"); }
    if (!g_res.frame2_ok) { fail = 1; printf("FAIL: begin_frame второго кадра (смена формата) вернул 0\n"); }
    if (g_swapchain_creates < 2) { fail = 1; printf("FAIL: ожидалось 2+ пересоздания swapchain, было %d\n", g_swapchain_creates); }
    if (g_pipeline_creates < 8) { fail = 1; printf("FAIL: ожидалось 8+ созданий конвейеров (4 init + 4 после смены формата), было %d\n", g_pipeline_creates); }
    if (!g_res.frame3_ok) { fail = 1; printf("FAIL: begin_frame третьего кадра вернул 0\n"); }
    if (g_res.off_w != 720 || g_res.off_h != 1280) {
        fail = 1; printf("FAIL: без апскейла оффскрин %ux%u, ожидалось 720x1280\n", g_res.off_w, g_res.off_h);
    }
    if (g_res.log_w != 720 || g_res.log_h != 1280) {
        fail = 1; printf("FAIL: логический размер %ux%u, ожидалось 720x1280\n", g_res.log_w, g_res.log_h);
    }
    if (fabsf(g_pc[0] - 2.0f / 720.0f) > 1e-9f || fabsf(g_pc[1] - 2.0f / 1280.0f) > 1e-9f) {
        fail = 1; printf("FAIL: push-константы кадра с апскейлом %g %g, ожидалось %g %g (шейдер должен делить на окно, не на оффскрин)\n",
                         g_pc[0], g_pc[1], 2.0f / 720.0f, 2.0f / 1280.0f);
    }
    if (g_blit_src_w != 720 || g_blit_src_h != 1280 || g_blit_dst_w != 720 || g_blit_dst_h != 1280) {
        fail = 1; printf("FAIL: blit %dx%d -> %dx%d, ожидалось 720x1280 -> 720x1280 (без апскейла)\n",
                         g_blit_src_w, g_blit_src_h, g_blit_dst_w, g_blit_dst_h);
    }
    if (g_blits < 3) { fail = 1; printf("FAIL: blit не вызывался на каждом кадре (было %d)\n", g_blits); }
    if (g_violations) { fail = 1; printf("FAIL: строгий драйвер поймал невалидный create-info: %s\n", g_violation_msg); }
    if (!fail) {
        printf("PASS: init + 3 кадра (оффскрин всегда с окно - апскейл убран) "
               "+ смена формата swapchain; pNext/flags чистые, конвейеров создано %d\n", g_pipeline_creates);
    }
    return fail;
}
