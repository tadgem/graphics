#include "gem/gem.h"
#include <sstream>

using namespace nlohmann;
using namespace gem;

static std::array<Im3d::Color, 4> s_colours
{
        Im3d::Color_Red,
        Im3d::Color_Magenta,
        Im3d::Color_Yellow,
        Im3d::Color_Green
};

template<size_t _NumSlices>
struct vxgi_data_n
{
  Texture m_voxel_texture; // 3D Texture (Voxel Data)
  glm::ivec3                          m_resolution;
  glm::vec3                           m_voxel_unit; // scale of each texel
  glm::vec3                           m_aabb_dim{200.0, 100.0, 200.0};
  AABB                                m_bounding_volume;
  std::array<glm::mat4, _NumSlices>   m_slice_vp_matrices;
  std::array<uint32_t, _NumSlices>    m_current_slice_indices;


  void update_bounding_volume(const glm::vec3& camera_pos)
  {
    m_voxel_unit = m_aabb_dim / glm::vec3(m_resolution);
    glm::vec3 half_dim = m_aabb_dim * 0.5f;
    m_bounding_volume.m_min = camera_pos - half_dim;
    m_bounding_volume.m_max = camera_pos + half_dim;

    for(size_t n = 0; n < _NumSlices; n++)
    {
      m_current_slice_indices[n] += _NumSlices;
      m_current_slice_indices[n] = m_current_slice_indices[n] >= m_resolution.y ? n : m_current_slice_indices[n];
      float btm = m_bounding_volume.m_min.y + (m_voxel_unit.y * m_current_slice_indices[n]);
      float top = m_bounding_volume.m_min.y + (m_voxel_unit.y * (m_current_slice_indices[n] + 1));
      glm::mat4 projection = glm::ortho(
          -half_dim.x,
          half_dim.x,
          btm,
          top,
          -half_dim.z,
          half_dim.z
          );

      glm::mat4 view = glm::translate(glm::mat4(1.0), camera_pos);
      m_slice_vp_matrices[n] =  projection * view;

    }

  }

  explicit vxgi_data_n(glm::ivec3 res)
  {
    m_resolution = res;
    for(size_t n = 0; n < _NumSlices; n++) {
      m_current_slice_indices[n] = static_cast<uint32_t>(n);

    }
    update_bounding_volume(glm::vec3(0.0));
  }

};

using VXGIData = vxgi_data_n<4>;

void on_im3d(GLRenderer & renderer, Scene & current_scene, VXGIData & vxgi,
             Camera & cam)
{
    vxgi.update_bounding_volume(glm::vec3(0.0f));

    DebugDraw::DrawBoundingBox(vxgi.m_bounding_volume.m_min, vxgi.m_bounding_volume.m_max, 2.0f, Im3d::Color_Pink);

    for(auto n = 0; n < 4; n++)
    {
      DebugDraw::DrawFrustum(vxgi.m_slice_vp_matrices[n], 1.0, s_colours[n]);
    }
    if (!current_scene.does_entity_exist((u32)renderer.m_last_selected_entity))
    {
        return;
    }

    if (!current_scene.m_registry.any_of<Mesh>(renderer.m_last_selected_entity))
    {
        return;
    }
    Mesh & meshc = current_scene.m_registry.get<Mesh>(renderer.m_last_selected_entity);
    Im3d::DrawAlignedBox(ToIm3D(meshc.m_transformed_aabb.m_min), ToIm3D(meshc.m_transformed_aabb.m_max));
}

void on_imgui(GLRenderer & renderer, Scene * s, glm::vec2 mouse_pos,
              DirectionalLight & dir2, Transform & cube_trans, std::vector<PointLight>& lights)
{
    if (s->does_entity_exist((u32) renderer.m_last_selected_entity))
    {
        EntityData & data = s->m_registry.get<EntityData>(renderer.m_last_selected_entity);
        ImGui::Begin(data.m_name.c_str());
        // do each component ImGui
        ImGui::End();
    }
    {
        renderer.on_imgui(Engine::assets);

        ImGui::Begin("Demo Settings");
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                    1000.0f / GPUBackend::selected()->m_imgui_io->Framerate,
                    GPUBackend::selected()->m_imgui_io->Framerate);

        ImGui::Text("Mouse Pos : %.3f, %.3f", mouse_pos.x, mouse_pos.y);
        ImGui::Text("Selected Entity ID : %d", renderer.m_last_selected_entity);
        ImGui::Separator();
        ImGui::Text("Debug Cube");
        ImGui::DragFloat3("Cube Position", &cube_trans.m_position[0], 1.0f);
        ImGui::DragFloat3("Cube Euler", &cube_trans.m_euler[0], 1.0f, 0.0, 360.0f);
        ImGui::DragFloat3("Cube Scale", &cube_trans.m_scale[0], 1.0f);
        ImGui::Separator();
        ImGui::Text("Lights");
        ImGui::ColorEdit3("Dir Light Colour", &dir2.colour[0]);
        ImGui::DragFloat3("Dir Light Rotation", &dir2.direction[0], 1.0f, 0.0f, 360.0f);
        ImGui::DragFloat("Dir Light Intensity", &dir2.intensity, 1.0f, 0.0f, 1000.0f);

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
}

int main()
{
    glm::ivec2 resolution = {1920, 1080};
    Engine::init();
    GLRenderer renderer{};
    renderer.init(Engine::assets, resolution);

    Camera cam{};
    DebugCameraController controller{};
    Scene * s = Engine::scenes.create_scene("test_scene");
    Entity e = s->create_entity("Daddalus");

    e.has_component<EntityData>();
    auto& data = e.get_component<EntityData>();
    Material mat(renderer.m_gbuffer_shader->m_handle, renderer.m_gbuffer_shader->m_data);
    e.add_component<Material>(renderer.m_gbuffer_shader->m_handle, renderer.m_gbuffer_shader->m_data);

    Engine::assets.load_asset("assets/models/sponza/Sponza.gltf", AssetType::model, [s, &renderer](Asset * a) {
        spdlog::info("adding model to scene");
        auto* ma = dynamic_cast<ModelAsset *>(a);
        ma->m_data.update_aabb();
        s->create_entity_from_model(ma->m_handle, ma->m_data, renderer.m_gbuffer_shader->m_handle, renderer.m_gbuffer_shader->m_data, glm::vec3(0.1), glm::vec3(0.0, 0.0, 0.0),
            {
                {"u_diffuse_map", TextureMapType::diffuse},
                {"u_normal_map", TextureMapType::normal},
                {"u_metallic_map", TextureMapType::metallicness},
                {"u_roughness_map", TextureMapType::roughness},
                {"u_ao_map", TextureMapType::ao}
            });

        nlohmann::json scene_json = Engine::scenes.save_scene(s);
        std::string scene_json_str = scene_json.dump();
        spdlog::info("finished adding model to scene, dumping scene json");
        spdlog::info(scene_json_str);
    });

    auto cube_entity = s->create_entity("Test Cube");
    auto& cube_trans = cube_entity.add_component<Transform>();
    auto& cube_mat = cube_entity.add_component<Material>(
        renderer.m_gbuffer_textureless_shader->m_handle,
                renderer.m_gbuffer_textureless_shader->m_data);
    cube_mat.set_uniform_value("u_diffuse_map", glm::vec3(1.0, 0.0, 0.0));
    cube_mat.set_uniform_value("u_metallic_map", 0.0f);
    cube_mat.set_uniform_value("u_roughness_map", 0.0f);
    cube_entity.add_component<MeshComponent>(
        MeshComponent{Shapes::s_torus_mesh, {}, 0});


    std::vector<PointLight> lights;
    DirectionalLight dir
    {
        {90.01f, 0.0f, 0.0f},
        {1.0f,1.0f,1.0f},
        2.75f
    };
    auto& dir2 = e.add_component<DirectionalLight>(dir);
    lights.push_back({ {0.0, 0.0, 0.0}, {255.0, 0.0, 0.0}, 10.0f});
    lights.push_back({ {10.0, 0.0, 10.0}, {255.0, 255.0, 0.0}, 20.0f });
    lights.push_back({ {-10.0, 0.0, -10.0}, {0.0, 255.0, 0.0}, 30.0f });
    lights.push_back({ {-10.0, 0.0, 10.0}, {0.0, 0.0, 255.0} , 40.0f});

    std::vector<Scene *> scenes{ s };
    VXGIData vxgi_dat ({256,256,256});
    vxgi_dat.update_bounding_volume(glm::vec3(0.0f));

    while (!GPUBackend::selected()->m_quit)
    {
        glEnable(GL_DEPTH_TEST);
        Engine::update();

        GPUBackend::selected()->process_sdl_event();
        GPUBackend::selected()->engine_pre_frame();
        glm::vec2 window_dim = GPUBackend::selected()->get_window_dim();
        renderer.pre_frame(cam);
        controller.update(window_dim, cam);
        cam.update(window_dim);

        for (auto* current_scene : scenes)
        {
            current_scene->on_update();
            on_im3d(renderer, *s, vxgi_dat, cam);
        }

        glm::vec2 mouse_pos = Input::get_mouse_position();
        if (Input::get_mouse_button(MouseButton::left) && !ImGui::GetIO().WantCaptureMouse)
        {
            renderer.get_mouse_entity(mouse_pos);
        }

        on_imgui(renderer, s, mouse_pos, dir2, cube_trans, lights);

        renderer.render(Engine::assets, cam, scenes);
        GPUBackend::selected()->engine_post_frame();
    }
    GPUBackend::selected()->engine_shut_down();
    Engine::shutdown();

    return 0;
}