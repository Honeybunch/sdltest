#include "camera.h"

#include <SDL2/SDL_events.h>

void camera_view_projection(const camera *c, float4x4 *vp) {
  float4x4 model_matrix = {0};
  transform_to_matrix(&model_matrix, &c->transform);

  float3 forward = f4tof3(model_matrix.row2);

  float4x4 view = {0};
  look_forward(&view, c->transform.position, forward, (float3){0, 1, 0});

  float4x4 proj = {0};
  perspective(&proj, c->fov, c->aspect, c->near, c->far);

  mulmf44(&proj, &view, vp);
}

void editor_camera_control(float delta_time_seconds, const SDL_Event *event,
                           editor_camera_controller *editor, camera *cam) {

  switch (event->type) {
  case SDL_KEYDOWN:
  case SDL_KEYUP: {
    SDL_Keysym keysym = event->key.keysym;

    editor_camera_state state = 0;

    if (keysym.scancode == SDL_SCANCODE_W) {
      state = EDITOR_CAMERA_MOVING_FORWARD;
    } else if (keysym.scancode == SDL_SCANCODE_A) {
      state = EDITOR_CAMERA_MOVING_LEFT;
    } else if (keysym.scancode == SDL_SCANCODE_S) {
      state = EDITOR_CAMERA_MOVING_BACKWARD;
    } else if (keysym.scancode == SDL_SCANCODE_D) {
      state = EDITOR_CAMERA_MOVING_RIGHT;
    }

    if (event->type == SDL_KEYDOWN) {
      editor->state |= state;
    } else if (event->type == SDL_KEYUP) {
      editor->state &= ~state;
    }

    break;
  }
  default:
    break;
  }

  if (editor->state) {
    float4x4 mat = {0};
    transform_to_matrix(&mat, &cam->transform);

    float3 right = f4tof3(mat.row0);
    float3 up = f4tof3(mat.row1);
    float3 forward = f4tof3(mat.row2);

    float delta_speed = editor->speed * delta_time_seconds;

    float3 velocity = {0};

    if (editor->state & EDITOR_CAMERA_MOVING_FORWARD) {
      velocity -= forward * delta_speed;
    }
    if (editor->state & EDITOR_CAMERA_MOVING_LEFT) {
      velocity -= right * delta_speed;
    }
    if (editor->state & EDITOR_CAMERA_MOVING_BACKWARD) {
      velocity += forward * delta_speed;
    }
    if (editor->state & EDITOR_CAMERA_MOVING_RIGHT) {
      velocity += right * delta_speed;
    }

    cam->transform.position += velocity;
  }
}