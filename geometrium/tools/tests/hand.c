/* Viewmodel topology and animation without a GPU or Android. */
#include "geometrium/geometrium_render_internal.h"
#include <stdio.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s\n",__func__,__LINE__,#x);exit(1);}}while(0)
#define CLOSE(a,b) CHECK(fabsf((a)-(b))<.00001f)
typedef struct {RendVertex v[4];float n[3];unsigned char light[4];uintptr_t material;} Face;
static Face faces[12],reference[12];
static int count,overlay,clears,flying,grounded=1;
static float px=8.5f,py=13,pz=8.5f,environment=1;
void geometrium_player_pos(float *x,float *y,float *z) {if(x)*x=px;if(y)*y=py;if(z)*z=pz;}
int geometrium_player_flying(void) {return flying;}
int geometrium_player_grounded(void) {return grounded;}
float geometrium_player_eye(void) {return GEOMETRIUM_PLAYER_EYE_HEIGHT;}
int geometrium_selected(void) {return 0;}
int geometrium_slot_block(int i) {CHECK(i==0);return BLOCK_GRASS;}
float voxel_world_light(float x,float y,float z) {CLOSE(x,px);CLOSE(y,py+GEOMETRIUM_PLAYER_EYE_HEIGHT);CLOSE(z,pz);return environment;}
RendMaterial *rend_material(int b,int f) {CHECK(b==BLOCK_GRASS && f>=0 && f<6);return (RendMaterial *)(uintptr_t)(f+1);}
void rend3d_viewmodel(int on) {CHECK(on!=overlay);overlay=on;}
int rend3d_project(float x,float y,float z,float *sx,float *sy) {
    CHECK(overlay && isfinite(x+y+z));
    if(z<.04f)return 0;
    *sx=screen_w*.5f+x/z*screen_h*.77f;*sy=screen_h*.5f-y/z*screen_h*.77f;return 1;
}
void rend3d_depth_clear(float x0,float y0,float x1,float y1) {CHECK(overlay && isfinite(x0+y0+x1+y1) && x1>x0 && y1>y0);clears++;}
void rend3d_polygon(const RendVertex *v,int n,float nx,float ny,float nz,uint32_t color,RendMaterial *material,const unsigned char *light) {
    (void)color;CHECK(overlay && clears && count<18 && n==4 && v && light);
    Face *f=&faces[count++];memcpy(f->v,v,sizeof(f->v));memcpy(f->light,light,4);
    f->n[0]=nx;f->n[1]=ny;f->n[2]=nz;f->material=(uintptr_t)material;
}
static void draw(void) {count=clears=0;geometrium_hand_draw();CHECK(count==12 && clears==1 && !overlay);}
static int same(const RendVertex *a,const RendVertex *b) {
    return fabsf(a->x-b->x)+fabsf(a->y-b->y)+fabsf(a->z-b->z)<.00001f;
}
static void topology(void) {
    for(int box=0;box<2;box++) {
        RendVertex unique[24];int uses[24]={0},n=0;
        float center[3]={0};
        for(int face=0;face<6;face++) {
            Face *f=&faces[box*6+face];
            CLOSE(f->n[0]*f->n[0]+f->n[1]*f->n[1]+f->n[2]*f->n[2],1);
            for(int i=0;i<4;i++) {
                RendVertex v=f->v[i];CHECK(isfinite(v.x+v.y+v.z+v.u+v.v) && v.z>.08f);
                center[0]+=v.x/24;center[1]+=v.y/24;center[2]+=v.z/24;
                int j=0;for(;j<n && !same(&v,&unique[j]);j++) {}
                if(j==n)unique[n++]=v;
                uses[j]++;
            }
            if(box==1)CHECK(f->material==(uintptr_t)(face+1));
            else CHECK(!f->material);
        }
        CHECK(n==8);for(int i=0;i<n;i++)CHECK(uses[i]==3);
        for(int face=0;face<6;face++) {
            Face *f=&faces[box*6+face];float offset=-1;
            for(int i=0;i<4;i++) {
                RendVertex v=f->v[i];float d=(v.x-center[0])*f->n[0]+(v.y-center[1])*f->n[1]+(v.z-center[2])*f->n[2];
                CHECK(d>.05f); /* all six faces must be away from the box center */
                if(i)CLOSE(d,offset);else offset=d;
            }
            Face *opposite=&faces[box*6+(face^1)];
            CLOSE(f->n[0]*opposite->n[0]+f->n[1]*opposite->n[1]+f->n[2]*opposite->n[2],-1);
        }
    }
}
static void test_geometry(void) {
    geometrium_hand_reset();draw();topology();memcpy(reference,faces,sizeof(faces));
    px=9000000;py=100;pz=-9000000;draw();CHECK(!memcmp(reference,faces,sizeof(faces)));
    px=pz=8.5f;py=13;
    for(int action=0;action<2;action++) {
        geometrium_hand_reset();if(action)geometrium_hand_equip();else geometrium_hand_swing();
        for(int i=0;i<24;i++) {geometrium_hand_update(1.f/60);draw();topology();}
    }
    for(int w=360;w<=1440;w+=360) {screen_w=w;screen_h=800;geometrium_hand_reset();draw();topology();}
    screen_w=960;screen_h=540;
    puts("PASS hand topology: two closed eight-corner cuboids (one rectangular arm + held block), offset face planes, orthonormal axes, local grass UV/materials and clipped animations");
}
static void test_light_and_flight(void) {
    geometrium_hand_reset();environment=1;draw();CHECK(faces[0].light[0]==255);
    environment=0;geometrium_hand_update(.05f);draw();
    int previous=faces[0].light[0];CHECK(previous>0 && previous<255);
    for(int frame=0;frame<30;frame++) {
        geometrium_hand_update(.05f);draw();int value=faces[0].light[0];CHECK(value<=previous);previous=value;
        for(int f=0;f<12;f++)for(int i=0;i<4;i++)CHECK(faces[f].light[i]==value);
    }
    CHECK(previous==0);
    environment=1;flying=1;grounded=0;geometrium_hand_reset();geometrium_hand_update(.05f);draw();memcpy(reference,faces,sizeof(faces));
    for(int i=0;i<30;i++) {px+=.1f;pz+=.12f;geometrium_hand_update(.05f);draw();CHECK(!memcmp(reference,faces,sizeof(faces)));}
    puts("PASS hand light changes smoothly as a whole; no sharp patches, no flight bob or world-position-dependent rotation");
}
int main(void) {screen_w=960;screen_h=540;test_geometry();test_light_and_flight();return 0;}
