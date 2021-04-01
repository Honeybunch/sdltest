#pragma once

#include <simd.h>

typedef struct camera {
  transform transform;
  float aspect;
  float fov;
  float near;
  float far;
} camera;

enum editor_camera_state_flags {
  EDITOR_CAMERA_NONE = 0x000,

  EDITOR_CAMERA_MOVING_FORWARD = 0x001,
  EDITOR_CAMERA_MOVING_BACKWARD = 0x002,
  EDITOR_CAMERA_MOVING_LEFT = 0x004,
  EDITOR_CAMERA_MOVING_RIGHT = 0x008,
  EDITOR_CAMERA_MOVING_UP = 0x010,
  EDITOR_CAMERA_MOVING_DOWN = 0x020,
  EDITOR_CAMERA_MOVING =
      EDITOR_CAMERA_MOVING_FORWARD | EDITOR_CAMERA_MOVING_BACKWARD |
      EDITOR_CAMERA_MOVING_LEFT | EDITOR_CAMERA_MOVING_RIGHT |
      EDITOR_CAMERA_MOVING_UP | EDITOR_CAMERA_MOVING_DOWN,

  EDITOR_CAMERA_LOOKING_LEFT = 0x040,
  EDITOR_CAMERA_LOOKING_RIGHT = 0x080,
  EDITOR_CAMERA_LOOKING_UP = 0x100,
  EDITOR_CAMERA_LOOKING_DOWN = 0x200,
  EDITOR_CAMERA_LOOKING = EDITOR_CAMERA_LOOKING_LEFT |
                          EDITOR_CAMERA_LOOKING_RIGHT |
                          EDITOR_CAMERA_LOOKING_UP | EDITOR_CAMERA_LOOKING_DOWN,
};
typedef uint32_t editor_camera_state;

typedef struct editor_camera_controller {
  float move_speed;
  float look_speed;
  editor_camera_state state;
} editor_camera_controller;

typedef union SDL_Event SDL_Event;

void camera_view_projection(const camera *c, float4x4 *vp);

void editor_camera_control(float delta_time_seconds, const SDL_Event *event,
                           editor_camera_controller *editor, camera *cam);