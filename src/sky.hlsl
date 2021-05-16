#include "common.hlsli"

struct hosek_coeffs{
  float a, b, i, c, d, e, f, h, g;
};

struct SkyData {
  float sun_size;
  float turbidity;
  float elevation;
  float overcast;

  float3 albedo_rgb;
  float3 sun_dir;
  float3 sun_color;
  float3 sun_tint_rgb;
  float3 radiance_xyz;

  hosek_coeffs coeffs_x;
  hosek_coeffs coeffs_y;
  hosek_coeffs coeffs_z;
};
ConstantBuffer<SkyData> sky_data : register(b1, space0); // Fragment Stage Only

struct VertexIn {
  float3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 view_pos : POSITION0;
};

Interpolators vert(VertexIn i) {
  Interpolators o;
  o.view_pos = i.local_pos;
  o.view_pos.xy *= -1.0;
  o.clip_pos = mul(float4(i.local_pos, 1.0), consts.mvp);
  return o;
}

float angle_of_dot(float dot) { return acos(max(dot, 0.0000001f)); }

float angle_between(float3 lhs, float3 rhs) {
  return acos(max(dot(lhs, rhs), 0.0000001f));
}

float3 XYZ_to_ACES2065_1(float3 color) {
  float3 o = float3(0, 0, 0);
  o.x = color.x * 1.0498110175f + color.y * 0.0000000000f +
        color.z * -0.0000974845f;
  o.y = color.x * -0.4959030231f + color.y * 1.3733130458f +
        color.z * 0.0982400361f;
  o.z = color.x * 0.0000000000f + color.y * 0.0000000000f +
        color.z * 0.9912520182f;
  return o;
}

float eval_hosek_coeffs(const hosek_coeffs coeffs, float cos_theta,
                               float gamma, float cos_gamma) {
  // Current coeffs ordering is AB I CDEF HG
  //                            01 2 3456 78
  const float expM = exp(coeffs.d * gamma);
  const float rayM = cos_gamma * cos_gamma;   // Rayleigh scattering
  const float mieM =
      (1.0f + rayM) /
      pow((1.0f + coeffs.g * coeffs.g - 2.0f * coeffs.g * cos_gamma),
           1.5f);
  const float zenith = sqrt(cos_theta); // vertical zenith gradient

  return (1.0f + coeffs.a * exp(coeffs.b / (cos_theta + 0.01f))
          ) *
         (1.0f + coeffs.c * expM // C
          + coeffs.e * rayM      // E
          + coeffs.f * mieM      // F
          + coeffs.h * zenith    // H
          + (coeffs.i - 1.0f)    // I
         );
}

float4 frag(Interpolators i) : SV_TARGET {
  const float FP16Scale = 0.0009765625f;

  float3 sample_dir = i.view_pos;
  float3 sun_dir = sky_data.sun_dir;

  float cos_theta = dot(sample_dir, float3(0, 1, 0));
  float cos_gamma = dot(sample_dir, sun_dir);

  float gamma = angle_of_dot(cos_gamma);
  float theta = angle_of_dot(cos_theta);

  float3 radiance;
  radiance.x = eval_hosek_coeffs(sky_data.coeffs_x, cos_theta, gamma, cos_gamma);
  radiance.y = eval_hosek_coeffs(sky_data.coeffs_y, cos_theta, gamma, cos_gamma);
  radiance.z = eval_hosek_coeffs(sky_data.coeffs_z, cos_theta, gamma, cos_gamma);
  radiance *= sky_data.radiance_xyz;

  radiance = XYZ_to_ACES2065_1(radiance);

  radiance += (sky_data.sun_color * sky_data.sun_tint_rgb) *
              (cos_gamma >= sky_data.sun_size);

  return float4(radiance, 1);
}