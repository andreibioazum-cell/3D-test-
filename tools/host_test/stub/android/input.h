/* Заглушка для хост-тестов. */
#ifndef HOST_STUB_ANDROID_INPUT_H
#define HOST_STUB_ANDROID_INPUT_H
#include <stdint.h>
#include <stddef.h>
typedef struct AInputEvent AInputEvent;
enum { AINPUT_EVENT_TYPE_MOTION = 2, AINPUT_EVENT_TYPE_KEY = 1 };
enum { AMOTION_EVENT_ACTION_DOWN = 0, AMOTION_EVENT_ACTION_UP = 1, AMOTION_EVENT_ACTION_MOVE = 2,
       AMOTION_EVENT_ACTION_POINTER_DOWN = 5, AMOTION_EVENT_ACTION_POINTER_UP = 6 };
enum { AKEY_EVENT_ACTION_DOWN = 0, AKEY_EVENT_ACTION_UP = 1, AKEY_EVENT_ACTION_MULTIPLE = 2 };
#define AMOTION_EVENT_ACTION_MASK 0xff
#define AMOTION_EVENT_ACTION_POINTER_INDEX_MASK 0xff00
#define AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT 8
int32_t AInputEvent_getType(const AInputEvent *event);
int32_t AMotionEvent_getAction(const AInputEvent *motion_event);
size_t AMotionEvent_getPointerCount(const AInputEvent *motion_event);
float AMotionEvent_getX(const AInputEvent *motion_event, size_t pointer_index);
float AMotionEvent_getY(const AInputEvent *motion_event, size_t pointer_index);
int32_t AMotionEvent_getPointerId(const AInputEvent *motion_event, size_t pointer_index);
int32_t AKeyEvent_getAction(const AInputEvent *key_event);
int32_t AKeyEvent_getKeyCode(const AInputEvent *key_event);
int32_t AKeyEvent_getMetaState(const AInputEvent *key_event);
#endif
