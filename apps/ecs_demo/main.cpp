#include <iostream>
#include "imgui.h"

#include "gl.h"
#include "texture.h" 
#include "shader.h"
#include "material.h"
#include "vertex.h"
#include "utils.h"
#include "model.h"
#include "camera.h"
#include "framebuffer.h"
#include "shape.h"
#include "voxelisation.h"

#include <sstream>
#include "im3d.h"
#include "im3d_gl.h"
#include "gtc/matrix_transform.hpp"
#include "gtc/quaternion.hpp"
#include "input.h"
#include "json.hpp"
#include "scene.h"
#include "asset.h"
#include "lights.h"
#include "transform.h"
#include "tech/vxgi.h"
#include "tech/gbuffer.h"
#include "tech/shadow.h"
#include "tech/lighting.h"
#include "tech/tech_utils.h"
#include "tech/taa.h"

using namespace nlohmann;
static glm::vec3 custom_orientation;


Im3d::Vec3 ToIm3D(glm::vec3& input)
{
    return { input.x, input.y, input.z };
}

static glm::mat4 last_vp = glm::mat4(1.0);
inline static u32 frame_index = 0;
inline static constexpr float gi_resolution_scale = 0.5;
inline static constexpr int shadow_resolution = 2048;
inline static constexpr int _3d_tex_res = 256;
const float SCREEN_W = 1280.0;
const float SCREEN_H = 720.0;



void dispatch_cone_tracing_pass_taa(shader& taa, shader& denoise, shader& present_shader, framebuffer& buffer_conetracing, framebuffer& buffer_conetracing_resolve, framebuffer& buffer_conetracing_denoise, framebuffer& history_buffer_conetracing, framebuffer& gbuffer, float aSigma, float aThreshold, float aKSigma, glm::ivec2 window_res)
{
    
    glViewport(0, 0, window_res.x * gi_resolution_scale, window_res.y * gi_resolution_scale);

    tech::taa::dispatch_taa_pass(taa, buffer_conetracing, buffer_conetracing_resolve, history_buffer_conetracing, gbuffer.m_colour_attachments[4], window_res);
    tech::utils::dispatch_denoise_image(denoise, buffer_conetracing_resolve, buffer_conetracing_denoise, aSigma, aThreshold, aKSigma, window_res);
    tech::utils::dispatch_present_image(present_shader, "u_image_sampler", 0, buffer_conetracing_denoise.m_colour_attachments.front());
    
    texture::bind_sampler_handle(0, GL_TEXTURE0);
    
    glViewport(0, 0, window_res.x, window_res.y);

}

void dispatch_visualize_3d_texture(voxel::grid& voxel_data, voxel::grid_visualiser& voxel_visualiser, camera& cam, model& sponza, shader& z_prepass_shader, glm::mat4& model)
{
    voxel_visualiser.texel_shape.use();
    auto& vs = voxel_visualiser.visual_shader;
    vs.use();
    vs.set_mat4("u_view_projection", cam.m_proj * cam.m_view);
    vs.set_ivec3("u_texture_resolution", voxel_data.resolution);
    vs.set_vec3("u_aabb.min", sponza.m_aabb.min);
    vs.set_vec3("u_aabb.max", sponza.m_aabb.max);
    vs.set_ivec3("u_voxel_group_resolution", glm::ivec3(voxel_visualiser.texel_resolution));
    vs.set_int("u_volume", 0);
    texture::bind_sampler_handle(voxel_data.voxel_texture.m_handle, GL_TEXTURE0, GL_TEXTURE_3D);
    glDrawElementsInstanced(GL_TRIANGLES, voxel_visualiser.index_count, GL_UNSIGNED_INT, 0, voxel_visualiser.total_invocations);
    texture::bind_sampler_handle(0, GL_TEXTURE0);

}

void dispatch_final_pass(shader& gi_combine, framebuffer& lightpass_buffer_resolve, framebuffer& buffer_conetracing_denoise, framebuffer& ssr_buffer)
{
    shapes::s_screen_quad.use();
    gi_combine.use();
    gi_combine.set_int("lighting_pass", 0);
    texture::bind_sampler_handle(lightpass_buffer_resolve.m_colour_attachments.front(), GL_TEXTURE0);
    gi_combine.set_int("cone_tracing_pass", 1);
    texture::bind_sampler_handle(buffer_conetracing_denoise.m_colour_attachments.front(), GL_TEXTURE1);
    gi_combine.set_int("ssr_pass", 2);
    texture::bind_sampler_handle(ssr_buffer.m_colour_attachments.front(), GL_TEXTURE2);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);

    texture::bind_sampler_handle(0, GL_TEXTURE0);
    texture::bind_sampler_handle(0, GL_TEXTURE1);
}

void draw_arrow(glm::vec3 pos, glm::vec3 dir)
{
    glm::vec3 end = pos + dir;
    Im3d::DrawArrow(ToIm3D(pos), ToIm3D(end));
    Im3d::DrawSphere(ToIm3D(end), 0.25f);
}

float get_aabb_area(aabb& bb)
{
    return glm::length(bb.max - bb.min) ;
}

int main()
{
    glm::ivec2 window_res{ SCREEN_W, SCREEN_H };
    engine::init(window_res);
    custom_orientation = glm::vec3(0, 1, 0);

    std::string gbuffer_vert = utils::load_string_from_path("assets/shaders/gbuffer.vert.glsl");
    std::string gbuffer_frag = utils::load_string_from_path("assets/shaders/gbuffer.frag.glsl");
    std::string gbuffer_floats_frag = utils::load_string_from_path("assets/shaders/gbuffer_floats.frag.glsl");
    std::string gbuffer_lighting_frag = utils::load_string_from_path("assets/shaders/lighting.frag.glsl");
    std::string visualize_3dtex_vert = utils::load_string_from_path("assets/shaders/visualize_3d_tex.vert.glsl");
    std::string visualize_3dtex_frag = utils::load_string_from_path("assets/shaders/visualize_3d_tex.frag.glsl");
    std::string present_vert = utils::load_string_from_path("assets/shaders/present.vert.glsl");
    std::string present_frag = utils::load_string_from_path("assets/shaders/present.frag.glsl");
    std::string dir_light_shadow_vert = utils::load_string_from_path("assets/shaders/dir_light_shadow.vert.glsl");
    std::string dir_light_shadow_frag = utils::load_string_from_path("assets/shaders/dir_light_shadow.frag.glsl");
    std::string voxelization_compute = utils::load_string_from_path("assets/shaders/gbuffer_voxelization.comp.glsl");
    std::string voxelization_mips_compute = utils::load_string_from_path("assets/shaders/voxel_mips.comp.glsl");
    std::string voxel_cone_tracing_frag = utils::load_string_from_path("assets/shaders/voxel_cone_tracing.frag.glsl");
    std::string ssr_frag = utils::load_string_from_path("assets/shaders/ssr.frag.glsl");
    std::string taa_frag = utils::load_string_from_path("assets/shaders/taa.frag.glsl");
    std::string denoise_frag = utils::load_string_from_path("assets/shaders/denoise.frag.glsl");
    std::string gi_combine_frag = utils::load_string_from_path("assets/shaders/gi_combine.frag.glsl");

    shader gbuffer_shader(gbuffer_vert, gbuffer_frag);
    shader gbuffer_floats_shader(gbuffer_vert, gbuffer_floats_frag);
    shader lighting_shader(present_vert, gbuffer_lighting_frag);
    shader present_shader(present_vert, present_frag);
    shader shadow_shader(dir_light_shadow_vert, dir_light_shadow_frag);
    shader visualize_3dtex(visualize_3dtex_vert, visualize_3dtex_frag);
    shader voxelization(voxelization_compute);
    shader voxelization_mips(voxelization_mips_compute);
    shader voxel_cone_tracing(present_vert, voxel_cone_tracing_frag);
    shader ssr(present_vert, ssr_frag);
    shader taa(present_vert, taa_frag);
    shader denoise(present_vert, denoise_frag);
    shader gi_combine(present_vert, gi_combine_frag);

    camera cam{};
    debug_camera_controller controller{};
    scene scene("test_scene");
    entity e = scene.create_entity("Daddalus");
    e.has_component<entity_data>();
    entity_data& data = e.get_component<entity_data>();
    material mat(gbuffer_shader);

    e.add_component<material>(gbuffer_shader);
    model sponza = model::load_model_from_path("assets/models/sponza/Sponza.gltf");
    framebuffer gbuffer{};

    scene.create_entity_from_model(sponza, gbuffer_shader, glm::vec3(0.1),
        {
            {"u_diffuse_map", texture_map_type::diffuse},
            {"u_normal_map", texture_map_type::normal},
            {"u_metallic_map", texture_map_type::metallicness},
            {"u_roughness_map", texture_map_type::roughness},
            {"u_ao_map", texture_map_type::ao}
        });

    gbuffer.bind();
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA, GL_NEAREST, GL_UNSIGNED_BYTE);
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT1, window_res.x, window_res.y, GL_RGBA32F, GL_NEAREST, GL_FLOAT);
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT2, window_res.x, window_res.y, GL_RGBA16F, GL_NEAREST, GL_FLOAT);
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT3, window_res.x, window_res.y, GL_RGBA16F, GL_NEAREST, GL_FLOAT);
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT4, window_res.x, window_res.y, GL_RG16F, GL_NEAREST, GL_FLOAT);
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT5, window_res.x, window_res.y, GL_RGBA16F, GL_NEAREST, GL_FLOAT);
    gbuffer.add_colour_attachment(GL_COLOR_ATTACHMENT6, window_res.x, window_res.y, GL_RGBA16F, GL_NEAREST, GL_FLOAT);


    gbuffer.add_depth_attachment_sampler_friendly(window_res.x, window_res.y);
    gbuffer.check();
    gbuffer.unbind();

    

    framebuffer dir_light_shadow_buffer{};
    dir_light_shadow_buffer.bind();
    gl_handle depthMap;
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
        shadow_resolution, shadow_resolution, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    dir_light_shadow_buffer.m_depth_attachment = depthMap;
    dir_light_shadow_buffer.m_height = shadow_resolution;
    dir_light_shadow_buffer.m_width = shadow_resolution;
    dir_light_shadow_buffer.unbind();

    framebuffer lightpass_buffer{};
    lightpass_buffer.bind();
    lightpass_buffer.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    lightpass_buffer.check();
    lightpass_buffer.unbind();

    framebuffer lightpass_buffer_resolve{};
    lightpass_buffer_resolve.bind();
    lightpass_buffer_resolve.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    lightpass_buffer_resolve.check();
    lightpass_buffer_resolve.unbind();

    framebuffer history_buffer_lighting{};
    history_buffer_lighting.bind();
    history_buffer_lighting.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    history_buffer_lighting.check();
    history_buffer_lighting.unbind();

    framebuffer history_buffer_position{};
    history_buffer_position.bind();
    history_buffer_position.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA32F, GL_LINEAR, GL_FLOAT);
    history_buffer_position.check();
    history_buffer_position.unbind();

    framebuffer history_buffer_conetracing{};
    history_buffer_conetracing.bind();
    history_buffer_conetracing.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    history_buffer_conetracing.check();
    history_buffer_conetracing.unbind();

    framebuffer buffer_conetracing{};
    buffer_conetracing.bind();
    buffer_conetracing.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x * gi_resolution_scale, window_res.y * gi_resolution_scale, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    buffer_conetracing.check();
    buffer_conetracing.unbind();

    framebuffer buffer_conetracing_denoise{};
    buffer_conetracing_denoise.bind();
    buffer_conetracing_denoise.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x * gi_resolution_scale, window_res.y* gi_resolution_scale, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    buffer_conetracing_denoise.check();
    buffer_conetracing_denoise.unbind();

    framebuffer buffer_conetracing_resolve{};
    buffer_conetracing_resolve.bind();
    buffer_conetracing_resolve.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x* gi_resolution_scale, window_res.y* gi_resolution_scale, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    buffer_conetracing_resolve.check();
    buffer_conetracing_resolve.unbind();

    framebuffer buffer_ssr{};
    buffer_ssr.bind();
    buffer_ssr.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x * gi_resolution_scale, window_res.y * gi_resolution_scale, GL_RGBA8, GL_NEAREST, GL_FLOAT);
    buffer_ssr.check();
    buffer_ssr.unbind();

    framebuffer buffer_ssr_resolve{};
    buffer_ssr_resolve.bind();
    buffer_ssr_resolve.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x * gi_resolution_scale, window_res.y * gi_resolution_scale, GL_RGBA8, GL_NEAREST, GL_FLOAT);
    buffer_ssr_resolve.check();
    buffer_ssr_resolve.unbind();


    framebuffer history_buffer_ssr{};
    history_buffer_ssr.bind();
    history_buffer_ssr.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x * gi_resolution_scale, window_res.y * gi_resolution_scale, GL_RGBA8, GL_NEAREST, GL_FLOAT);
    history_buffer_ssr.check();
    history_buffer_ssr.unbind();


    framebuffer final_pass{};
    final_pass.bind();
    final_pass.add_colour_attachment(GL_COLOR_ATTACHMENT0, window_res.x, window_res.y, GL_RGBA16F, GL_LINEAR, GL_FLOAT);
    final_pass.check();
    final_pass.unbind();

    dir_light dir
    {
        {90.01f, 0.0f, 0.0f},
        {1.0f,1.0f,1.0f},
        2.75f
    };
    std::vector<point_light> lights;
    lights.push_back({ {0.0, 0.0, 0.0}, {255.0, 0.0, 0.0}, 10.0f});
    lights.push_back({ {10.0, 0.0, 10.0}, {255.0, 255.0, 0.0}, 20.0f });
    lights.push_back({ {-10.0, 0.0, -10.0}, {0.0, 255.0, 0.0}, 30.0f });
    lights.push_back({ {-10.0, 0.0, 10.0}, {0.0, 0.0, 255.0} , 40.0f});


    constexpr glm::vec3 _3d_tex_res_vec = { _3d_tex_res, _3d_tex_res, _3d_tex_res };

    glm::vec3 pos = glm::vec3(0.0f);
    glm::vec3 euler = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(0.1f);
    glm::mat4 model = utils::get_model_matrix(pos, euler, scale);
    glm::mat3 normal = utils::get_normal_matrix(model);
    sponza.m_aabb = utils::transform_aabb(sponza.m_aabb, model);

    voxel::grid              voxel_data = voxel::create_grid(_3d_tex_res_vec, sponza.m_aabb);
    voxel::grid_visualiser   voxel_visualiser = voxel::create_grid_visualiser(voxel_data, visualize_3dtex, 8);

    glm::vec3 aabb_dim = sponza.m_aabb.max - sponza.m_aabb.min;
    glm::vec3 unit = glm::vec3((aabb_dim.x / _3d_tex_res), (aabb_dim.y / _3d_tex_res), (aabb_dim.z / _3d_tex_res));
    glm::vec3 n_unit = glm::normalize(unit);


    voxelization.use();
    voxelization.set_int("u_gbuffer_pos", 0);
    voxelization.set_int("u_gbuffer_lighting", 1);
    voxelization.set_vec3("u_voxel_resolution", glm::vec3( _3d_tex_res));

    bool draw_debug_3d_texture = false;
    bool draw_direct_lighting = true;
    bool draw_direct_lighting_no_taa = true;
    bool draw_cone_tracing_pass = true;
    bool draw_cone_tracing_pass_no_taa = true;
    bool draw_ssr = true;
    bool draw_final_pass = true;

    bool draw_im3d = true;

    auto im3d_s =  im3d_gl::load_im3d();

    glm::vec3 aabb_half_extent = (sponza.m_aabb.max - sponza.m_aabb.min) / 2.0f;
    glm::vec3 aabb_center = sponza.m_aabb.min + aabb_half_extent;
    glm::mat4 aabb_model = utils::get_model_matrix(aabb_center, glm::vec3(0.0f), aabb_half_extent * 2.0f);

    GLfloat aSigma = 2.0f;
    GLfloat aThreshold = 0.1f;
    GLfloat aKSigma =  2.0f;
    GLfloat vxgi_cone_distance = 70.0;
    GLfloat diffuse_spec_mix = 0.5;

    while (!engine::s_quit)
    {
        glm::mat4 model = utils::get_model_matrix(pos, euler, scale);

        glEnable(GL_DEPTH_TEST);
        
        engine::process_sdl_event();
        engine::engine_pre_frame();        
        glm::mat4 mvp = cam.m_proj * cam.m_view * model;
        glm::vec2 window_dim = engine::get_window_dim();
        im3d_gl::new_frame_im3d(im3d_s);
        
        controller.update(window_dim, cam);
        cam.update(window_dim);
        scene.on_update();


        // compute
        tech::vxgi::dispatch_gbuffer_voxelization(voxelization, sponza.m_aabb, voxel_data, gbuffer, lightpass_buffer_resolve, window_res);

        tech::vxgi::dispatch_gen_voxel_mips(voxelization_mips, voxel_data, _3d_tex_res_vec);
        
        tech::gbuffer::dispatch_gbuffer(frame_index, gbuffer, history_buffer_position, gbuffer_shader, cam, scene, window_res);

        tech::shadow::dispatch_shadow_pass(dir_light_shadow_buffer, shadow_shader, dir, scene, window_res);

        tech::lighting::dispatch_light_pass(lighting_shader, lightpass_buffer, gbuffer, dir_light_shadow_buffer, cam, lights, dir);
                
        if (draw_direct_lighting)
        {
            tech::taa::dispatch_taa_pass(taa, lightpass_buffer, lightpass_buffer_resolve, history_buffer_lighting, gbuffer.m_colour_attachments[4], window_res);
        }

        if (draw_cone_tracing_pass || draw_cone_tracing_pass_no_taa)
        {
            tech::vxgi::dispatch_cone_tracing_pass(voxel_cone_tracing, voxel_data, buffer_conetracing, gbuffer, window_res, sponza.m_aabb, _3d_tex_res_vec, cam, vxgi_cone_distance, gi_resolution_scale, diffuse_spec_mix);
        }

        if (draw_direct_lighting)
        {
            tech::utils::dispatch_present_image(present_shader, "u_image_sampler", 0, lightpass_buffer_resolve.m_colour_attachments.front());
        }

        if (draw_ssr)
        {
            shapes::s_screen_quad.use();
            buffer_ssr.bind();
            glViewport(0, 0, window_res.x * gi_resolution_scale, window_res.y * gi_resolution_scale);
            glClearColor(0, 0, 0, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDepthMask(GL_FALSE);
            ssr.use();
            ssr.set_float("SCR_WIDTH", SCREEN_W);
            ssr.set_float("SCR_HEIGHT", SCREEN_H);
            ssr.set_mat4("projection", cam.m_proj);
            ssr.set_mat4("invProjection", glm::inverse(cam.m_proj));
            ssr.set_mat4("rotation", cam.get_rotation_matrix());

            ssr.set_int("gNormal", 0);
            texture::bind_sampler_handle(gbuffer.m_colour_attachments[2], GL_TEXTURE0);

            ssr.set_int("colorBuffer", 1);
            texture::bind_sampler_handle(lightpass_buffer_resolve.m_colour_attachments.front(), GL_TEXTURE1);
            
            ssr.set_int("depthMap", 2);
            texture::bind_sampler_handle(gbuffer.m_depth_attachment, GL_TEXTURE2);

            glStencilFunc(GL_EQUAL, 1, 0xFF);
            glStencilMask(0x00);

            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0); 
            glDepthMask(GL_TRUE);

            tech::taa::dispatch_taa_pass(taa, buffer_ssr, buffer_ssr_resolve, history_buffer_ssr, gbuffer.m_colour_attachments[4], window_res);
            glViewport(0, 0, window_res.x, window_res.y);
        }

        if (draw_cone_tracing_pass)
        {
            dispatch_cone_tracing_pass_taa(taa, denoise, present_shader, buffer_conetracing, buffer_conetracing_resolve, buffer_conetracing_denoise, history_buffer_conetracing, gbuffer, aSigma, aThreshold, aKSigma, window_res);
        }
        if (draw_cone_tracing_pass_no_taa)
        {
            tech::utils::dispatch_present_image(present_shader, "u_image_sampler", 0, buffer_conetracing.m_colour_attachments.front());
        }
        if (draw_direct_lighting_no_taa)
        {
            tech::utils::dispatch_present_image(present_shader, "u_image_sampler", 0, lightpass_buffer.m_colour_attachments.front());

        }

        // copy history buffers
        tech::utils::blit_to_fb(history_buffer_lighting, present_shader, "u_image_sampler", 0, lightpass_buffer_resolve.m_colour_attachments[0]);
        tech::utils::blit_to_fb(history_buffer_position, present_shader, "u_image_sampler", 0, gbuffer.m_colour_attachments[1]);
        tech::utils::blit_to_fb(history_buffer_conetracing, present_shader, "u_image_sampler", 0, buffer_conetracing_denoise.m_colour_attachments.front());
        glClear(GL_DEPTH_BUFFER_BIT);

        if (draw_debug_3d_texture)
        {
            dispatch_visualize_3d_texture(voxel_data, voxel_visualiser, cam, sponza, shadow_shader, model);
        }

        if (draw_final_pass)
        {
            final_pass.bind();
            dispatch_final_pass(gi_combine, lightpass_buffer_resolve, buffer_conetracing_denoise, buffer_ssr_resolve);
            final_pass.unbind();
            tech::utils::dispatch_present_image(present_shader, "u_image_sampler", 0, final_pass.m_colour_attachments.front());
        }

        {
            ImGui::Begin("Transform Debug");
            ImGui::DragFloat3("Position", &pos[0]);
            ImGui::DragFloat3("Euler", &euler[0]);
            ImGui::DragFloat3("Scale", &scale[0]);

            ImGui::End();

            ImGui::Begin("Hello, world!");                          
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / engine::s_imgui_io->Framerate, engine::s_imgui_io->Framerate);
            ImGui::Separator();
            ImGui::Checkbox("Render 3D Voxel Grid", &draw_debug_3d_texture);
            ImGui::Checkbox("Render Final Pass", &draw_final_pass);
            ImGui::Checkbox("Render Direct Lighting Pass", &draw_direct_lighting);
            ImGui::Checkbox("Render Direct Lighting Pass NO TAA", &draw_direct_lighting_no_taa);
            ImGui::Checkbox("Render Cone Tracing Pass", &draw_cone_tracing_pass);
            ImGui::Checkbox("Render SSR", &draw_ssr);
            ImGui::Checkbox("Render Cone Tracing Pass NO TAA", &draw_cone_tracing_pass_no_taa);
            ImGui::Checkbox("Render IM3D", &draw_im3d);
            ImGui::Separator();
            ImGui::Text("VXGI Settings");
            ImGui::DragFloat("Trace Distance", &vxgi_cone_distance);
            ImGui::DragFloat("Diffuse / Spec Mix", &diffuse_spec_mix, 1.0f, 0.0f, 1.0f);
            ImGui::Separator();
            ImGui::Text("Denoise Settings");
            ImGui::DragFloat("Sigma", &aSigma);
            ImGui::DragFloat("Threshold", &aThreshold);
            ImGui::DragFloat("KSigma", &aKSigma);
            ImGui::Separator();

            ImGui::DragFloat3("Camera Position", &cam.m_pos[0]);
            ImGui::DragFloat3("Camera Euler", &cam.m_euler[0]);
            ImGui::Separator();
            ImGui::DragFloat3("Orientation Test", &custom_orientation[0]);
            ImGui::Separator();
            ImGui::Text("Level Bounding Volume Area %.2f", get_aabb_area(sponza.m_aabb));
            glm::vec3 dim = sponza.m_aabb.max - sponza.m_aabb.min;
            ImGui::Text("Level Bounding Volume Dimensions %.2f,%.2f,%.2f", dim.x, dim.y, dim.z);
            ImGui::Separator();
            ImGui::Text("Lights");
            ImGui::ColorEdit3("Dir Light Colour", &dir.colour[0]);
            ImGui::DragFloat3("Dir Light Rotation", &dir.direction[0], 1.0f, 0.0f, 360.0f);
            ImGui::DragFloat("Dir Light Intensity", &dir.intensity, 1.0f, 0.0f, 1000.0f);

            for (int l = 0; l < lights.size(); l++)
            {
                std::stringstream name;
                name << "Light " << l;
                ImGui::PushID(l);
                if (ImGui::TreeNode(name.str().c_str()))
                {
                    ImGui::DragFloat3("Position", &lights[l].position[0]);
                    ImGui::ColorEdit3("Colour", &lights[l].colour[0]);
                    ImGui::DragFloat("Radius", &lights[l].radius);
                    ImGui::DragFloat("Intensity", &lights[l].intensity);
                    ImGui::TreePop();
                }
                ImGui::PopID();

            }

            ImGui::End();
        }
        if (draw_im3d)
        {
            im3d_gl::end_frame_im3d(im3d_s, {window_res.x, window_res.y}, cam);
        }
        else
        {
            Im3d::EndFrame();
        }
        engine::engine_post_frame();
    }
    im3d_gl::shutdown_im3d(im3d_s);
    engine::engine_shut_down();

    return 0;
}