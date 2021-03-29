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
  EDITOR_CAMERA_NONE = 0x00,
  EDITOR_CAMERA_MOVING_FORWARD = 0x01,
  EDITOR_CAMERA_MOVING_BACKWARD = 0x02,
  EDITOR_CAMERA_MOVING_LEFT = 0x04,
  EDITOR_CAMERA_MOVING_RIGHT = 0x08,
  EDITOR_CAMERA_MOVING_UP = 0x10,
  EDITOR_CAMERA_MOVING_DOWN = 0x20,
};
typedef uint32_t editor_camera_state;

typedef struct editor_camera_controller {
  float speed;
  editor_camera_state state;
} editor_camera_controller;

typedef union SDL_Event SDL_Event;

void camera_view_projection(const camera *c, float4x4 *vp);

void editor_camera_control(float delta_time_seconds, const SDL_Event *event,
                           editor_camera_controller *editor, camera *cam);