#include <sstream>
#define GLM_ENABLE_EXPERIMENTAL
#include "gem/camera.h"
#include "gem/gl/gl_dbg.h"
#include "gem/gl/gl_framebuffer.h"
#include "gem/gl/tech/lighting.h"
#include "gem/profile.h"
#include "gem/shape.h"
#include "gem/texture.h"
#include "gem/utils.h"
#include "gtc/quaternion.hpp"
namespace gem {
namespace open_gl {

void tech::PBRLighting::dispatch_light_pass(
    GLShader &lighting_shader, GLFramebuffer &lighting_buffer,
    GLFramebuffer &gbuffer, GLFramebuffer &dir_light_shadow_buffer,
    Camera &cam, std::vector<PointLight> &point_lights,
    DirectionalLight &sun) {
  ZoneScoped;
  GEM_GPU_MARKER("Lighting Pass");
  lighting_buffer.bind();
  lighting_shader.use();
  Shapes::s_screen_quad.use();

  lighting_shader.set_int("u_diffuse_map", 0);
  lighting_shader.set_int("u_position_map", 1);
  lighting_shader.set_int("u_normal_map", 2);
  lighting_shader.set_int("u_pbr_map", 3);
  lighting_shader.set_int("u_dir_light_shadow_map", 4);

  glm::vec3 dir =
      glm::quat(glm::radians(sun.direction)) * glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec3 lightPos = glm::vec3(0.0) - (dir * 100.0f);

  lighting_shader.set_vec3("u_cam_pos", cam.m_pos);
  lighting_shader.set_vec3("u_dir_light_pos", lightPos);

  lighting_shader.set_vec3("u_dir_light.direction",
                           Utils::get_forward(sun.direction));
  lighting_shader.set_vec3("u_dir_light.colour", sun.colour);
  lighting_shader.set_mat4("u_dir_light.light_space_matrix",
                           sun.light_space_matrix);
  lighting_shader.set_float("u_dir_light.intensity", sun.intensity);

  int num_point_lights = std::min((int)point_lights.size(), 16);

  for (int i = 0; i < num_point_lights; i++) {
    std::stringstream pos_name;
    pos_name << "u_point_lights[" << i << "].position";
    std::stringstream col_name;
    col_name << "u_point_lights[" << i << "].colour";
    std::stringstream rad_name;
    rad_name << "u_point_lights[" << i << "].radius";
    std::stringstream int_name;
    int_name << "u_point_lights[" << i << "].intensity";

    lighting_shader.set_vec3(pos_name.str(), point_lights[i].position);
    lighting_shader.set_vec3(col_name.str(), point_lights[i].colour);
    lighting_shader.set_float(rad_name.str(), point_lights[i].radius);
    lighting_shader.set_float(int_name.str(), point_lights[i].intensity);
  }

  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[0], GL_TEXTURE0);
  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[1], GL_TEXTURE1);
  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[2], GL_TEXTURE2);
  Texture::bind_sampler_handle(gbuffer.m_colour_attachments[3], GL_TEXTURE3);
  Texture::bind_sampler_handle(dir_light_shadow_buffer.m_depth_attachment,
                               GL_TEXTURE4);

  // bind all maps
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
  lighting_buffer.unbind();
}
} // namespace open_gl
} // namespace gem