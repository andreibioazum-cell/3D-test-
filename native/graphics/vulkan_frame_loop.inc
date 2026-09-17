/* Инициализация и выключение бэкенда, покадровый цикл begin/end/cancel.
 * Часть Vulkan-бэкенда: включается из vulkan_backend.inc, всё собирается
 * одним translation unit'ом (graphics.c), поэтому состояние общее. */

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

    if (!ds_vk_create_all_pipelines()) { ds_vk_shutdown_backend(); return 0; }

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
    vk_begun = 0;
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

static int ds_vk_begin_frame_backend(uint32_t w, uint32_t h, uint32_t ow, uint32_t oh) {
    if (!vk_ready) return 0;
    if (w == 0 || h == 0 || ow == 0 || oh == 0) return 0;
    vk_log_w = w; vk_log_h = h;
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
    /* Если новый swapchain оказался другого формата, чем тот, под который
     * собраны render pass и конвейеры, пересобираем их: framebuffer оффскрина
     * строится в формате swapchain, а render pass собран под старый. */
    if (vk_sw_format != vk_rp_format && !ds_vk_rebuild_pipelines()) return 0;
    /* Заливки текстур ждём на хосте ДО записи команд кадра: к моменту
     * submit'а кадра transfer уже завершён (фенс), гонок отправок нет. */
    ds_vk_process_pending_uploads();
    if (!ds_vk_ensure_offscreen(ow, oh, vk_sw_format)) return 0;

    /* Слот кадра: новый - только если прошлый начатый кадр был завершён
     * (submit или полный сброс); иначе переиспользуем текущий слот вместе
     * с его уже захваченным изображением. */
    if (!vk_begun) {
        vk_frame_cur = (vk_frame_cur + 1) % DS_VK_FRAMES_IN_FLIGHT;
        vk_begun = 1;
    }
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
    ri.renderArea.extent.width = ow;
    ri.renderArea.extent.height = oh;
    ri.clearValueCount = 1;
    ri.pClearValues = &clear;
    vkCmdBeginRenderPass(f->cmd, &ri, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp = { 0.0f, 0.0f, (float)ow, (float)oh, 0.0f, 1.0f };
    VkRect2D sc = { { 0, 0 }, { ow, oh } };
    vkCmdSetViewport(f->cmd, 0, 1, &vp);
    vkCmdSetScissor(f->cmd, 0, 1, &sc);
    return 1;
}


static void ds_vk_end_frame_backend(void) {
    DsVkFrame *f = &vk_frames[vk_frame_cur];
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
        return; /* показывать нечего: vk_begun остаётся 1, слот переиспользуется */
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
    vk_begun = 0;
}
