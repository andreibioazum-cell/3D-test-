/* Память и буферы, дескрипторы, загрузка текстур и шрифтового атласа на GPU.
 * Часть Vulkan-бэкенда: включается из vulkan_backend.inc, всё собирается
 * одним translation unit'ом (graphics.c), поэтому состояние общее. */

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
