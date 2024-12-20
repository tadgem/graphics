#define GLM_ENABLE_EXPERIMENTAL
#include "gem/gl/tech/vxgi.h"
#include "gem/backend.h"
#include "gem/camera.h"
#include "gem/gl/gl_dbg.h"
#include "gem/profile.h"

namespace gem {
namespace open_gl {

void tech::VXGI::dispatch_gbuffer_voxelization(GLShader &voxelization,
                                               Voxel::Grid &voxel_data,
                                               GLFramebuffer &gbuffer,
                                               GLFramebuffer &lightpass_buffer,
                                               glm::ivec2 window_res) {
  ZoneScoped;
  GEM_GPU_MARKER("GBuffer Voxelisation");
  voxelization.use();
  voxelization.set_int("u_gbuffer_pos", 0);
  voxelization.set_int("u_gbuffer_lighting", 1);
  voxelization.set_vec3("u_voxel_resolution", voxel_data.resolution);
  voxelization.set_vec2("u_input_resolution", {window_res.x, window_res.y});
  voxelization.set_vec3("u_aabb.min", voxel_data.current_bounding_box.m_min);
  voxelization.set_vec3("u_aabb.max", voxel_data.current_bounding_box.m_max);
  voxelization.set_vec3("u_voxel_unit", voxel_data.voxel_unit);
  Texture::bind_image_handle(voxel_data.voxel_texture.m_handle, 0, 0,
                             GL_RGBA16F);
  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[1], GL_TEXTURE0);
  Texture::bind_sampler_handle(lightpass_buffer.m_colour_attachments[0],
                               GL_TEXTURE1);
  glAssert(glDispatchCompute(window_res.x / 10, window_res.y / 10, 1));
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void tech::VXGI::dispatch_gen_voxel_mips(GLShader &voxelization_mips,
                                         Voxel::Grid &voxel_data,
                                         glm::vec3 _3d_tex_res_vec) {
  ZoneScoped;
  GEM_GPU_MARKER("Voxel Mips Generation");
  constexpr int MAX_MIPS = 5;
  voxelization_mips.use();
  // for each mip in remaining_mipps
  glm::vec3 last_mip_resolution = _3d_tex_res_vec;
  glm::vec3 current_mip_resolution = _3d_tex_res_vec / 2.0f;
  for (int i = 1; i < MAX_MIPS; i++) {
    glBindTexture(GL_TEXTURE_3D, voxel_data.voxel_texture.m_handle);
    Texture::bind_image_handle(voxel_data.voxel_texture.m_handle, 0, i,
                               GL_RGBA16F);
    Texture::bind_image_handle(voxel_data.voxel_texture.m_handle, 1, i - 1,
                               GL_RGBA16F);
    voxelization_mips.set_vec3("u_current_resolution", current_mip_resolution);
    glm::ivec3 dispatch_dims =
        glm::ivec3(int(current_mip_resolution.x), int(current_mip_resolution.y),
                   int(current_mip_resolution.z));
    glAssert(glDispatchCompute(dispatch_dims.x / 8, dispatch_dims.y / 8,
                               dispatch_dims.z / 8));
    current_mip_resolution /= 2.0f;
    last_mip_resolution /= 2.0f;
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
  }
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void tech::VXGI::dispatch_cone_tracing_pass(
    GLShader &voxel_cone_tracing, Voxel::Grid &voxel_data,
    GLFramebuffer &buffer_conetracing, GLFramebuffer &gbuffer,
    glm::ivec2 window_res, AABB &bounding_volume, glm::vec3 _3d_tex_res,
    Camera &cam, float max_trace_distance, float resolution_scale,
    float diffuse_spec_mix) {
  ZoneScoped;
  GEM_GPU_MARKER("Cone Tracing Pass");

  glBindTexture(GL_TEXTURE_3D, voxel_data.voxel_texture.m_handle);

  glViewport(0, 0, window_res.x * resolution_scale,
             window_res.y * resolution_scale);
  Shapes::s_screen_quad.use();
  buffer_conetracing.bind();
  voxel_cone_tracing.use();
  voxel_cone_tracing.set_vec3("u_aabb.min", bounding_volume.m_min);
  voxel_cone_tracing.set_vec3("u_aabb.max", bounding_volume.m_max);
  voxel_cone_tracing.set_vec3("u_voxel_resolution", _3d_tex_res);
  voxel_cone_tracing.set_int("u_position_map", 0);
  voxel_cone_tracing.set_vec3("u_cam_position", cam.m_pos);
  voxel_cone_tracing.set_float("u_max_trace_distance", max_trace_distance);
  voxel_cone_tracing.set_float("u_diffuse_spec_mix", diffuse_spec_mix);

  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[1], GL_TEXTURE0);
  voxel_cone_tracing.set_int("u_normal_map", 1);
  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[2], GL_TEXTURE1);
  voxel_cone_tracing.set_int("u_voxel_map", 2);
  Texture::bind_sampler_handle(voxel_data.voxel_texture.m_handle, GL_TEXTURE2,
                               GL_TEXTURE_3D);
  voxel_cone_tracing.set_int("u_colour_map", 3);
  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[0], GL_TEXTURE3);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  buffer_conetracing.unbind();
  Texture::bind_sampler_handle(0, GL_TEXTURE0);
  Texture::bind_sampler_handle(0, GL_TEXTURE1);
  Texture::bind_sampler_handle(0, GL_TEXTURE2);
  Texture::bind_sampler_handle(0, GL_TEXTURE3);
  glViewport(0, 0, window_res.x, window_res.y);
}

void tech::VXGI::dispatch_blit_voxel(GLShader &blit_voxel,
                                     Voxel::Grid &voxel_data,
                                     glm::vec3 _3d_tex_res_vec) {
  ZoneScoped;
  GEM_GPU_MARKER("Voxel History Blit");
  blit_voxel.use();
  Texture::bind_image_handle(voxel_data.voxel_texture.m_handle, 0, 0,
                             GL_RGBA16F);
  blit_voxel.set_vec3("u_voxel_resolution", _3d_tex_res_vec);

  glAssert(glDispatchCompute(_3d_tex_res_vec.x / 8, _3d_tex_res_vec.y / 8,
                             _3d_tex_res_vec.z / 8));
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void tech::VXGI::dispatch_clear_voxel(GLShader &clear_voxel,
                                      Voxel::Grid &voxel_data,
                                      glm::vec3 _3d_tex_res_vec) {
  ZoneScoped;
  GEM_GPU_MARKER("Clear Voxel Grid");
  clear_voxel.use();
  Texture::bind_image_handle(voxel_data.voxel_texture.m_handle, 0, 0,
                             GL_RGBA16F);
  glAssert(glDispatchCompute(_3d_tex_res_vec.x / 8, _3d_tex_res_vec.y / 8,
                             _3d_tex_res_vec.z / 8));
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void tech::VXGI::dispatch_voxelisation_gbuffer_pass(
    GLShader &gbuffer_shader, Voxel::Grid &grid_data, GLFramebuffer gbuffer,
    GLFramebuffer lighting_buffer, glm::ivec2 window_res) {}
} // namespace open_gl
} // namespace gem