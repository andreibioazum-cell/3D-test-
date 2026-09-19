/* Команды скрипта -> пачки геометрии, запись draw/blit, render pass и пересборка конвейеров.
 * Часть Vulkan-бэкенда: включается из vulkan_backend.inc, всё собирается
 * одним translation unit'ом (graphics.c), поэтому состояние общее. */

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
            case DS_CMD_RECT_ROT:
                /* pipeline 0: обычное альфа-смешивание, поэтому повёрнутый
                 * прямоугольник хитбокса прозрачный, а не «залитый» наглухо,
                 * как roundrect на fill_span-конвейере. */
                geo_rect_rot(c->v.rot.x, c->v.rot.y, c->v.rot.w, c->v.rot.h,
                             c->v.rot.ang, c->v.rot.c);
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
    /* Координаты скриптов → NDC: масштаб берём из логического размера окна,
     * иначе при апскейле весь интерфейс вырос бы в scale раз. */
    uint32_t lw = vk_log_w ? vk_log_w : vk_off_w, lh = vk_log_h ? vk_log_h : vk_off_h;
    float pc[4] = { 2.0f / (float)lw, 2.0f / (float)lh, -1.0f, -1.0f };
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
        /* 1:1 копируем без фильтрации; растягивание уменьшенного оффскрина
         * (автомасштаб) - LINEAR, если формат позволяет: картинка мягкая,
         * а не «пиксели», которыми прежний апскейл бесил игрока. */
        VkFilter filter = VK_FILTER_NEAREST;
        if (vk_blit_linear && (vk_off_w != vk_sw_extent.width ||
                               vk_off_h != vk_sw_extent.height))
            filter = VK_FILTER_LINEAR;
        vkCmdBlitImage(cmd, vk_off_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       sw_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, filter);
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
    int ok = ds_vk_check_result(vkCreateRenderPass(vk_device, &ri, NULL, &vk_renderpass), "vkCreateRenderPass");
    if (ok) vk_rp_format = vk_sw_format;
    return ok;
}

/* Создание всех четырёх конвейеров под текущий render pass. SPIR-V спечён,
 * поэтому модули можно пересоздавать в любой момент (при смене формата). */
static int ds_vk_create_all_pipelines(void) {
    VkShaderModule frag_solid = VK_NULL_HANDLE, frag_image = VK_NULL_HANDLE, frag_tint = VK_NULL_HANDLE;
    int ok = ds_vk_create_shader_module(ds_spirv_solid_frag,
                                        sizeof(ds_spirv_solid_frag) / sizeof(uint32_t), &frag_solid) &&
             ds_vk_create_shader_module(ds_spirv_image_frag,
                                        sizeof(ds_spirv_image_frag) / sizeof(uint32_t), &frag_image) &&
             ds_vk_create_shader_module(ds_spirv_tint_frag,
                                        sizeof(ds_spirv_tint_frag) / sizeof(uint32_t), &frag_tint) &&
             ds_vk_create_pipeline(frag_solid, vk_layout_solid, 1, &vk_pipe_solid) &&
             ds_vk_create_pipeline(frag_solid, vk_layout_solid, 0, &vk_pipe_replace) &&
             ds_vk_create_pipeline(frag_image, vk_layout_image, 1, &vk_pipe_image) &&
             ds_vk_create_pipeline(frag_tint, vk_layout_tint, 1, &vk_pipe_tint);
    /* Модули больше не нужны при любом исходе - конвейеры их скопировали. */
    if (frag_solid != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_solid, NULL);
    if (frag_image != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_image, NULL);
    if (frag_tint != VK_NULL_HANDLE) vkDestroyShaderModule(vk_device, frag_tint, NULL);
    return ok;
}

/* Пересборка render pass и конвейеров после смены формата swapchain
 * (поворот/пересоздание окна может дать другой формат). Покадровый оффскрин
 * подтянется под новый формат в ds_vk_ensure_offscreen; если этого не сделать,
 * формат привязки framebuffer не совпадёт с форматом render pass - драйвер
 * (особенно бюджетные Mali) на этом роняет device или падает с SIGSEGV. */
static int ds_vk_rebuild_pipelines(void) {
    if (vk_device) vkDeviceWaitIdle(vk_device);
    if (vk_pipe_solid != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_solid, NULL);
    if (vk_pipe_replace != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_replace, NULL);
    if (vk_pipe_image != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_image, NULL);
    if (vk_pipe_tint != VK_NULL_HANDLE) vkDestroyPipeline(vk_device, vk_pipe_tint, NULL);
    vk_pipe_solid = vk_pipe_replace = vk_pipe_image = vk_pipe_tint = VK_NULL_HANDLE;
    if (vk_renderpass != VK_NULL_HANDLE) vkDestroyRenderPass(vk_device, vk_renderpass, NULL);
    vk_renderpass = VK_NULL_HANDLE;
    ds_vk_destroy_offscreen();
    if (!ds_vk_create_renderpass()) return 0;
    if (!ds_vk_create_all_pipelines()) return 0;
    return 1;
}
