/* Конвейеры со спечёнными SPIR-V шейдерами и оффскрин-таргет кадра.
 * Часть Vulkan-бэкенда: включается из vulkan_backend.inc, всё собирается
 * одним translation unit'ом (graphics.c), поэтому состояние общее. */

/* --- конвейеры --- */

static int ds_vk_create_shader_module(const uint32_t *code, size_t words, VkShaderModule *out) {
    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = words * sizeof(uint32_t);
    ci.pCode = code;
    return ds_vk_check_result(vkCreateShaderModule(vk_device, &ci, NULL, out), "vkCreateShaderModule");
}

static int ds_vk_create_pipeline(VkShaderModule frag, VkPipelineLayout layout, int blend, VkPipeline *out) {
    /* Массив ОБЯЗАН быть нулевым: по спецификации Vulkan pNext = NULL и
     * flags = 0 (reserved for future use). Без инициализации в этих полях
     * остаётся мусор стека, а загрузчик и драйвер GPU обходят цепочку pNext
     * каждого stage'а: мусор-указатель приводит к разыменованию несуществующего
     * адреса прямо внутри драйвера (на Mali — SIGSEGV в libGLES_mali.so,
     * tombstone «null pointer dereference» при создании конвейера в
     * ds_graphics_init - игра вылетала при запуске). */
    VkPipelineShaderStageCreateInfo stages[2] = { 0 };
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = VK_NULL_HANDLE;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    /* При ошибке vkCreateGraphicsPipelines значение хендла по спецификации
     * НЕ ОПРЕДЕЛЕНО: гоняем его в NULL, чтобы выключение не уничтожало мусор. */
    *out = VK_NULL_HANDLE;
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
    /* Вершинный модуль создан здесь - уничтожаем здесь. Фрагментный модуль
     * принадлежит вызывающему коду: один frag_solid используется двумя
     * конвейерами (обычным и replace), уничтожать его после первого же
     * создания конвейера значило бы строить второй по освобождённому
     * хендлу - мгновенный краш на устройстве. */
    vkDestroyShaderModule(vk_device, stages[0].module, NULL);
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
