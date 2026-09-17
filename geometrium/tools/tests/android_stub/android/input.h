#ifndef STUB_ANDROID_INPUT_H
#define STUB_ANDROID_INPUT_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AInputEvent AInputEvent;
enum { AINPUT_EVENT_TYPE_KEY = 1, AINPUT_EVENT_TYPE_MOTION = 2 };
enum { AKEY_EVENT_ACTION_DOWN = 0, AKEY_EVENT_ACTION_UP = 1 };
enum {
  AMOTION_EVENT_ACTION_MASK = 0xff,
  AMOTION_EVENT_ACTION_POINTER_INDEX_MASK = 0xff00,
  AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT = 8,
  AMOTION_EVENT_ACTION_DOWN = 0, AMOTION_EVENT_ACTION_UP = 1,
  AMOTION_EVENT_ACTION_MOVE = 2, AMOTION_EVENT_ACTION_CANCEL = 3,
  AMOTION_EVENT_ACTION_POINTER_DOWN = 5, AMOTION_EVENT_ACTION_POINTER_UP = 6
};
int32_t AInputEvent_getType(const AInputEvent *);
size_t AMotionEvent_getPointerCount(const AInputEvent *);
int32_t AMotionEvent_getAction(const AInputEvent *);
float AMotionEvent_getX(const AInputEvent *, size_t);
float AMotionEvent_getY(const AInputEvent *, size_t);
int32_t AMotionEvent_getPointerId(const AInputEvent *, size_t);
int32_t AKeyEvent_getAction(const AInputEvent *);
int32_t AKeyEvent_getKeyCode(const AInputEvent *);
#ifdef __cplusplus
}
#endif
#endif
