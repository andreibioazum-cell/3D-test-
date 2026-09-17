/* Swapchain (создание/пересоздание/уничтожение) и кадры в полёте.
 * Часть Vulkan-бэкенда: включается из vulkan_backend.inc, всё собирается
 * одним translation unit'ом (graphics.c), поэтому состояние общее. */

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
    /* VK_FORMAT_UNDEFINED означает "подойдёт любой" - выбираем сами. */
    vk_sw_format = chosen.format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : chosen.format;

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
    /* preTransform = IDENTITY — обязательно на Android. Изображения swapchain
     * живут в системе координат ОКНА (ландшафт, как видит пользователь), а
     * поворот окна относительно дисплея уже применяет системный композитор —
     * этот поворот и есть caps.currentTransform (ROTATE_90 в ландшафте).
     * Если задать preTransform = currentTransform (как в некоторых примерах),
     * движок презентации проворачивает изображение ЕЩЁ на 90°: в ландшафте
     * игра оказывается «вертикальной» и растянутой. */
    si.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
        : caps.currentTransform;
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
