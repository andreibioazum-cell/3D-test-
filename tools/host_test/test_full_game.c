#define VK_USE_PLATFORM_ANDROID_KHR
#include <stdarg.h>
#include <sys/stat.h>
#include <jni.h>
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap);
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


/* ==== настоящая игра: скрипты, рантайм, сеть, звук, Vulkan-бэкенд ==== */
#include "game/game.c"
#include "runtime.c"
#include "net.c"
/* Звук на устройстве живёт в JNI; на хосте включаем тот же код с заглушкой jni.h:
 * snd_vm == NULL, поэтому бэкенд честно не стартует, как «нет аудио» на устройстве. */
#define __ANDROID__ 1
#include "sound.c"
#undef __ANDROID__
#include "graphics.c"

/* ==== Android-заглушки с НАСТОЯЩИМИ ассетами из game/assets ==== */
struct AAsset { FILE *fp; long len; };
static int dummy_amgr_storage;
AAsset *AAssetManager_open(AAssetManager *mgr, const char *name, int mode) {
    (void)mgr; (void)mode;
    char path[512];
    snprintf(path, sizeof path, "game/assets/%s", name);
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    AAsset *a = (AAsset *)calloc(1, sizeof *a);
    if (!a) { fclose(fp); return NULL; }
    a->fp = fp;
    fseek(fp, 0, SEEK_END); a->len = ftell(fp); fseek(fp, 0, SEEK_SET);
    return a;
}
off_t AAsset_getLength(AAsset *a) { return a ? (off_t)a->len : 0; }
int AAsset_read(AAsset *a, void *buf, size_t n) { return a ? (int)fread(buf, 1, n, a->fp) : -1; }
int AAsset_close(AAsset *a) { if (!a) return 0; fclose(a->fp); free(a); return 0; }
int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    (void)prio; (void)tag;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
    return 0;
}
int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list ap) {
    (void)prio; (void)tag;
    vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    return 0;
}

/* ==== фейковое окно: RGBA-буфер 1280x720, честный lock/unlock ==== */
static uint32_t g_win_pixels[1280 * 720];
static int g_win_locked;
int32_t ANativeWindow_getWidth(ANativeWindow *w) { (void)w; return 1280; }
int32_t ANativeWindow_getHeight(ANativeWindow *w) { (void)w; return 720; }
int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format) {
    (void)w; (void)width; (void)height; (void)format; return 0;
}
int ANativeWindow_lock(ANativeWindow *w, ANativeWindow_Buffer *out, void *rb) {
    (void)w; (void)rb;
    if (g_win_locked) return -1;
    out->bits = g_win_pixels; out->width = 1280; out->height = 720;
    out->stride = 1280; out->format = WINDOW_FORMAT_RGBA_8888;
    g_win_locked = 1;
    return 0;
}
int ANativeWindow_unlockAndPost(ANativeWindow *w) { (void)w; g_win_locked = 0; return 0; }
ANativeWindow *ANativeWindow_acquire(ANativeWindow *w) { return w; }
void ANativeWindow_release(ANativeWindow *w) { (void)w; }

/* Отчёт о падении - тот же модуль, что в main.c. */
#include "native/crash_report.inc"
#include <signal.h>

/* ==== клавиатура: на устройстве это android_keyboard.inc (JNI IME) ==== */
static int kb_up; static char kb_buf[256];
void keyboard_show(void) { kb_up = 1; }
void keyboard_hide(void) { kb_up = 0; }
int keyboard_visible(void) { return kb_up; }
const char *keyboard_get_text(void) { return kb_buf; }
const char *keyboard_get_raw(void) { return kb_buf; }
void keyboard_clear(void) { kb_buf[0] = 0; }
int keyboard_enter_pressed(void) { return 0; }
void keyboard_type(const char *s) { if (s) strncat(kb_buf, s, sizeof kb_buf - strlen(kb_buf) - 1); }
void keyboard_commit_utf8(const char *s) { keyboard_type(s); }
void keyboard_backspace(void) { size_t n = strlen(kb_buf); while (n > 0 && ((unsigned char)kb_buf[n-1] & 0xC0) == 0x80) n--; if (n > 0) n--; kb_buf[n] = 0; }

/* ==== симуляция android_main: INIT_WINDOW -> кадры -> TERM -> INIT ==== */
/* Обёртки как в main.c: ds_call_protected ждёт void (*)(void *). */
static void protected_update(void *u) { (void)u; update(); }
static void protected_draw(void *u) { draw((Buffer *)u); }
static void protected_init(void *u) { init((AAssetManager *)u); }
static void app_init_window(void) {
    static int dummy_window;
    ANativeWindow *win = (ANativeWindow *)&dummy_window;
    screen_w = 1280; screen_h = 720;
    if (!ds_graphics_init((AAssetManager *)&dummy_amgr_storage, win)) { fprintf(stderr, "HARNESS: ds_graphics_init failed\n"); exit(3); }
    ds_sound_init((AAssetManager *)&dummy_amgr_storage);
    ds_sound_resume();
    ds_call_protected(protected_init, (AAssetManager *)&dummy_amgr_storage, "init");
}
static Buffer g_fb;
static void app_frame(int n) {
    (void)n;
    dt = 1.0 / 60.0;
    ds_call_protected(protected_update, NULL, "update");
    g_fb.width = 1280; g_fb.height = 720; g_fb.stride = 1280;
    if (ds_graphics_begin_frame(&g_fb)) {
        ds_call_protected(protected_draw, &g_fb, "draw");
        ds_graphics_end_frame();
    }
}

int main(void) {
    setvbuf(stderr, NULL, _IONBF, 0);
    /* Сохранения игры (progress.dirty и пр.) - во временную папку, чтобы
     * прогон теста не сорил в корне репозитория: на устройстве этот путь
     * задаёт main.c через net_set_data_path(internalDataPath). */
    mkdir("/tmp/cb4_host_test", 0777);
    net_set_data_path("/tmp/cb4_host_test");
    app_init_window();
    /* Лобби: 120 кадров (~2 секунды) */
    for (int i = 0; i < 120; i++) app_frame(i);
    fprintf(stderr, "HARNESS: lobby done, state=%f\n", game_state);

    /* Настройки: открыли, потыкали строки, вернулись */
    game_state = ST_SETTINGS;
    for (int i = 0; i < 30; i++) app_frame(i);
    touch(640, 300, 0, 0); touch(640, 300, 1, 0);
    for (int i = 0; i < 30; i++) app_frame(i);

    /* Бой: соло-матч, 600 кадров, с ударом в середине */
    game_state = ST_SOLO;
    ds_fn_init_game();
    for (int i = 0; i < 300; i++) app_frame(i);
    ds_fn_start_punch_now();
    for (int i = 0; i < 300; i++) app_frame(i);
    fprintf(stderr, "HARNESS: battle done\n");

    /* Сворачивание и возврат: окно умерло, окно новое - путь, который чинит PR */
    ds_graphics_shutdown();
    ds_sound_shutdown();
    app_init_window();
    for (int i = 0; i < 240; i++) app_frame(i);
    fprintf(stderr, "HARNESS: resume done\n");

    /* Шторм пересозданий: лаунчер/поворот/IME на реальном устройстве делают
     * TERM+INIT много раз подряд и в разные размеры окна. Каждое - полный
     * цикл: destroy device/swapchain, новая поверхность, новое окно. */
    for (int cycle = 0; cycle < 5; cycle++) {
        ds_graphics_shutdown();
        ds_sound_shutdown();
        app_init_window();
        for (int i = 0; i < 30; i++) app_frame(i);
    }
    fprintf(stderr, "HARNESS: recreate storm done\n");

    ds_graphics_shutdown();
    ds_sound_shutdown();

    /* Диагностика падения: крошки -> отчёт -> чтение -> CPU-режим с экраном
     * отчёта и обычными кадрами игры. ds_crash_report_write зовётся напрямую
     * (без реального сигнала: ASan перехватывает его раньше нашего хендлера). */
    fprintf(stderr, "HARNESS: crash-report scenario\n");
    ds_crash_report_init("/tmp/cb4_host_test");
    ds_crumb("boot"); ds_crumb("jni"); ds_crumb("win"); ds_crumb("gfx-vk-init");
    ds_crash_report_write(SIGABRT, (void *)(uintptr_t)0xdeadbeef);
    char report[DS_CRASH_REPORT_MAX];
    if (!ds_crash_report_load(report, sizeof report)) {
        printf("FAIL: отчёт о падении не записан\n"); return 1;
    }
    if (!strstr(report, "signal 6") || !strstr(report, "gfx-vk-init")) {
        printf("FAIL: в отчёте нет сигнала или крошек:\n%s\n", report); return 1;
    }
    {
        static int dummy_window2;
        ANativeWindow *win2 = (ANativeWindow *)&dummy_window2;
        screen_w = 1280; screen_h = 720;
        if (!ds_graphics_init_cpu((AAssetManager *)&dummy_amgr_storage, win2)) {
            printf("FAIL: ds_graphics_init_cpu вернул 0\n"); return 1;
        }
        Buffer fb2; memset(&fb2, 0, sizeof fb2);
        for (int i = 0; i < 8; i++) {
            dt = 1.0 / 60.0;
            if (i == 0) ds_call_protected(init, (AAssetManager *)&dummy_amgr_storage, "init");
            ds_call_protected(update, NULL, "update");
            if (!ds_graphics_begin_frame_cpu(&fb2)) {
                printf("FAIL: begin_frame_cpu вернул 0\n"); return 1;
            }
            if (i < 3) ds_graphics_error_screen(report); /* экран отчёта */
            else ds_call_protected(protected_draw, &fb2, "draw"); /* игра в CPU-режиме */
            ds_graphics_end_frame_cpu(&fb2);
        }
        /* Кадр лобби (нарисован выше в CPU-режиме) - в BMP для глазной проверки:
         * фон, кнопки, текст и текстуры обязаны быть на своих местах. */
        {
            FILE *f = fopen("/tmp/cb4_cpu_lobby.bmp", "wb");
            if (f) {
                uint32_t W = 1280, H = 720, row = W * 3, pad = (4 - (row % 4)) % 4;
                uint32_t sz = 54 + (row + pad) * H;
                uint8_t hdr[54] = { 'B','M' };
                memcpy(hdr + 2, &sz, 4); uint32_t off = 54; memcpy(hdr + 10, &off, 4);
                uint32_t hs = 40; memcpy(hdr + 14, &hs, 4);
                memcpy(hdr + 18, &W, 4); memcpy(hdr + 22, &H, 4);
                uint16_t planes = 1, bpp = 24; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
                fwrite(hdr, 1, 54, f);
                uint8_t zero[4] = {0,0,0,0};
                for (uint32_t y = H; y-- > 0;) {
                    for (uint32_t x = 0; x < W; x++) {
                        uint32_t p = g_win_pixels[y * 1280 + x];
                        uint8_t rgb[3] = { (uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p };
                        fwrite(rgb, 1, 3, f);
                    }
                    fwrite(zero, 1, pad, f);
                }
                fclose(f);
                fprintf(stderr, "HARNESS: cpu lobby frame -> /tmp/cb4_cpu_lobby.bmp\n");
            }
        }
        /* Отчёт стёрли - следующая загрузка прочитает пустоту. */
        ds_crash_report_clear();
        if (ds_crash_report_load(report, sizeof report)) {
            printf("FAIL: отчёт не стёрлся\n"); return 1;
        }
        ds_graphics_shutdown_cpu();
        ds_graphics_shutdown();
    }
    printf("PASS: полная игра + crash-report + CPU-режим на фейковом драйвере\n");
    return 0;
}
