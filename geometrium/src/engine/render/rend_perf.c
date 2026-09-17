/* FPS counts actual frame intervals. Render quality uses measured CPU cost,
 * never network/vsync delays, with hysteresis to avoid resolution flicker.
 * AUTO adapts the internal resolution to hold ~60 fps; the fixed levels
 * (Options menu) pin it for predictable cost. */
#include "rend_internal.h"
static double frame_seconds,render_seconds;
static int frame_count,render_count,fast_windows,render_scale=1,base_scale=1,last_w,last_h;
static int quality=REND_QUALITY_AUTO;
static float fps=-1;
void rend_perf_reset(void) {
    frame_seconds=render_seconds=0;frame_count=render_count=fast_windows=last_w=last_h=0;fps=-1;
}
void rend_perf_frame(double interval) {
    if (!isfinite(interval) || interval<=0) return;
    frame_seconds+=interval;frame_count++;
    if (frame_seconds>=.5) {fps=(float)(frame_count/frame_seconds);frame_seconds=0;frame_count=0;}
}
float rend_fps(void) { return fps; }
void rend_set_quality(int value) {
    if (value<REND_QUALITY_AUTO || value>REND_QUALITY_LOW) value=REND_QUALITY_AUTO;
    if (value==quality) return;
    quality=value;last_w=0; /* recompute the scale for the pinned level */
}
int rend_render_scale(int width,int height) {
    if (width!=last_w || height!=last_h) {
        last_w=width;last_h=height;base_scale=1;
        /* Native up to a ~1.6 MP internal budget (e.g. 1080x1280): smaller
         * internal frames keep the rasterizer on a 60 fps budget on phones. */
        while (base_scale<4 && (double)(width/base_scale)*(height/base_scale)>1600000) base_scale++;
        render_scale=base_scale;render_count=fast_windows=0;render_seconds=0;
    }
    if (quality==REND_QUALITY_HIGH) { render_scale=1; return render_scale; }
    if (quality==REND_QUALITY_MEDIUM) { render_scale=base_scale<2 ? 2 : base_scale; return render_scale; }
    if (quality==REND_QUALITY_LOW) { render_scale=base_scale<3 ? 3 : base_scale; return render_scale; }
    return render_scale;
}
void rend_render_time(double elapsed) {
    if (!isfinite(elapsed) || elapsed<=0) return;
    if (quality!=REND_QUALITY_AUTO) return;   /* pinned levels never adapt */
    render_seconds+=elapsed;render_count++;
    if (render_count<30) return;
    double average=render_seconds/render_count;
    /* Drop a tier the moment the 3D pass threatens the 16.7 ms frame budget;
     * recover only after sustained headroom, so fps stays at the top. */
    if (average>.0167 && render_scale<4) {render_scale++;fast_windows=0;}
    else if (average<.006 && render_scale>base_scale) {
        if (++fast_windows>=3) {render_scale--;fast_windows=0;}
    } else fast_windows=0;
    render_count=0;render_seconds=0;
}
