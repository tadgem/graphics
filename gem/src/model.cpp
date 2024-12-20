#define GLM_ENABLE_EXPERIMENTAL
#include "gem/model.h"
#include "assimp/Importer.hpp"
#include "assimp/cimport.h"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "gem/hash_string.h"
#include "gem/profile.h"
#include "glm.hpp"
#include "spdlog/spdlog.h"

namespace gem {

static glm::vec3 assimp_to_glm(const aiVector3D &aiVec) {
  ZoneScoped;
  return glm::vec3(aiVec.x, aiVec.y, aiVec.z);
}

static glm::vec2 assimp_to_glm(const aiVector2D &aiVec) {
  ZoneScoped;
  return glm::vec2(aiVec.x, aiVec.y);
}

TextureEntry::TextureEntry(TextureMapType tmt, AssetHandle ah,
                             const std::string &path, Texture *data)
    : m_handle(ah), m_map_type(tmt), m_path(path), m_texture(data) {
  ZoneScoped;
}

void process_mesh(Model &model, aiMesh *m, aiNode *node, const aiScene *scene,
                  bool use_entries,
                  std::vector<Model::MeshEntry> &mesh_entries) {
  ZoneScoped;
  bool hasPositions = m->HasPositions();
  bool hasUVs = m->HasTextureCoords(0);
  bool hasNormals = m->HasNormals();
  bool hasIndices = m->HasFaces();

  std::vector<aiMaterialProperty *> properties;
  aiMaterial *mMaterial = scene->mMaterials[m->mMaterialIndex];

  for (int i = 0; i < mMaterial->mNumProperties; i++) {
    aiMaterialProperty *prop = mMaterial->mProperties[i];
    properties.push_back(prop);
  }

  if (use_entries) {
    Model::MeshEntry entry{};
    if (hasPositions && hasUVs && hasNormals) {
      for (unsigned int i = 0; i < m->mNumVertices; i++) {
        entry.m_positions.push_back(
            {m->mVertices[i].x, m->mVertices[i].y, m->mVertices[i].z});
        entry.m_normals.push_back(
            {m->mNormals[i].x, m->mNormals[i].y, m->mNormals[i].z});
        entry.m_uvs.push_back(
            {m->mTextureCoords[0][i].x, m->mTextureCoords[0][i].y});
      }
    }

    if (hasIndices) {
      for (unsigned int i = 0; i < m->mNumFaces; i++) {
        aiFace currentFace = m->mFaces[i];
        if (currentFace.mNumIndices != 3) {
          spdlog::error("Attempting to import a m with non triangular face "
                        "structure! cannot load this model");
          return;
        }
        for (unsigned int index = 0; index < m->mFaces[i].mNumIndices;
             index++) {
          entry.m_indices.push_back(
              static_cast<uint32_t>(m->mFaces[i].mIndices[index]));
        }
      }
    }
    AABB bb = {{m->mAABB.mMin.x, m->mAABB.mMin.y, m->mAABB.mMin.z},
               {m->mAABB.mMax.x, m->mAABB.mMax.y, m->mAABB.mMax.z}};
    entry.m_mesh_aabb = bb;
    entry.m_material_index = m->mMaterialIndex;
    mesh_entries.push_back(entry);
  } else {
    std::vector<float> verts;
    VAOBuilder mesh_builder{};
    mesh_builder.begin();

    if (hasPositions && hasUVs && hasNormals) {
      for (unsigned int i = 0; i < m->mNumVertices; i++) {
        verts.push_back(m->mVertices[i].x);
        verts.push_back(m->mVertices[i].y);
        verts.push_back(m->mVertices[i].z);
        verts.push_back(m->mNormals[i].x);
        verts.push_back(m->mNormals[i].y);
        verts.push_back(m->mNormals[i].z);
        verts.push_back(m->mTextureCoords[0][i].x);
        verts.push_back(m->mTextureCoords[0][i].y);
      }
      mesh_builder.add_vertex_buffer(verts);
      mesh_builder.add_vertex_attribute(0, 8 * sizeof(float), 3);
      mesh_builder.add_vertex_attribute(1, 8 * sizeof(float), 3);
      mesh_builder.add_vertex_attribute(2, 8 * sizeof(float), 2);
    }

    std::vector<uint32_t> indices;
    if (hasIndices) {
      for (unsigned int i = 0; i < m->mNumFaces; i++) {
        aiFace currentFace = m->mFaces[i];
        if (currentFace.mNumIndices != 3) {
          spdlog::error("Attempting to import a m with non triangular face "
                        "structure! cannot load this m.");
          return;
        }
        for (unsigned int index = 0; index < m->mFaces[i].mNumIndices;
             index++) {
          indices.push_back(
              static_cast<uint32_t>(m->mFaces[i].mIndices[index]));
        }
      }
      mesh_builder.add_index_buffer(indices);
    }
    AABB bb = {{m->mAABB.mMin.x, m->mAABB.mMin.y, m->mAABB.mMin.z},
               {m->mAABB.mMax.x, m->mAABB.mMax.y, m->mAABB.mMax.z}};

    Mesh new_mesh{};
    new_mesh.m_vao = mesh_builder.build();
    new_mesh.m_index_count = indices.size();
    new_mesh.m_original_aabb = bb;
    new_mesh.m_material_index = m->mMaterialIndex;

    model.m_meshes.push_back(new_mesh);
  }
}

void process_node(Model &model, aiNode *node, const aiScene *scene,
                  bool use_entries,
                  std::vector<Model::MeshEntry> &mesh_entries) {
  ZoneScoped;
  if (node->mNumMeshes > 0) {
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
      unsigned int sceneIndex = node->mMeshes[i];
      aiMesh *mesh = scene->mMeshes[sceneIndex];
      process_mesh(model, mesh, node, scene, use_entries, mesh_entries);
    }
  }

  if (node->mNumChildren == 0) {
    return;
  }

  for (unsigned int i = 0; i < node->mNumChildren; i++) {
    process_node(model, node->mChildren[i], scene, use_entries, mesh_entries);
  }
}

void get_material_texture(const std::string &directory, aiMaterial *material,
                          Model::MaterialEntry &mat,
                          aiTextureType ass_texture_type,
                          TextureMapType gl_texture_type) {
  ZoneScoped;
  uint32_t tex_count = aiGetMaterialTextureCount(material, ass_texture_type);
  if (tex_count > 0) {
    aiString result_path;
    aiGetMaterialTexture(material, ass_texture_type, 0, &result_path);
    std::string final_path = directory + std::string(result_path.C_Str());
    spdlog::info("Loading Texture at Path: {}", final_path);
    Texture *tex = new Texture(final_path);

    AssetHandle h(final_path, AssetType::texture);
    TextureEntry tex_entry(gl_texture_type, h, final_path, tex);

    mat.m_material_maps[gl_texture_type] = tex_entry;
  }
}

void get_material_texture_entry(const std::string &directory,
                                aiMaterial *material,
                                Model::MaterialEntry &mat,
                                aiTextureType ass_texture_type,
                                TextureMapType gl_texture_type,
                                std::vector<TextureEntry> &texture_entries) {
  ZoneScoped;
  uint32_t tex_count = aiGetMaterialTextureCount(material, ass_texture_type);
  if (tex_count > 0) {
    aiString resultPath;
    aiGetMaterialTexture(material, ass_texture_type, 0, &resultPath);
    std::string final_path = directory + std::string(resultPath.C_Str());

    AssetHandle h(final_path, AssetType::texture);
    TextureEntry texture_entry(gl_texture_type, h, final_path, nullptr);
    mat.m_material_maps[gl_texture_type] = texture_entry;
    texture_entries.push_back(texture_entry);
  }
}

Model Model::load_model_and_textures_from_path(const std::string &path) {
  ZoneScoped;
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      path.c_str(), aiProcess_Triangulate | aiProcess_CalcTangentSpace |
                        aiProcess_OptimizeMeshes | aiProcess_GenSmoothNormals |
                        aiProcess_OptimizeGraph | aiProcess_FixInfacingNormals |
                        aiProcess_FindInvalidData | aiProcess_GenBoundingBoxes);
  //
  if (scene == nullptr) {
    return {};
  }

  std::vector<Model::MeshEntry> mesh_entries{};

  Model m{};
  AABB model_aabb{};
  process_node(m, scene->mRootNode, scene, false, mesh_entries);

  for (auto &mesh : m.m_meshes) {
    if (mesh.m_original_aabb.m_min.x < model_aabb.m_min.x) {
      model_aabb.m_min.x = mesh.m_original_aabb.m_min.x;
    }
    if (mesh.m_original_aabb.m_min.y < model_aabb.m_min.y) {
      model_aabb.m_min.y = mesh.m_original_aabb.m_min.y;
    }
    if (mesh.m_original_aabb.m_min.z < model_aabb.m_min.z) {
      model_aabb.m_min.z = mesh.m_original_aabb.m_min.z;
    }

    if (mesh.m_original_aabb.m_max.x > model_aabb.m_max.x) {
      model_aabb.m_max.x = mesh.m_original_aabb.m_max.x;
    }
    if (mesh.m_original_aabb.m_max.y > model_aabb.m_max.y) {
      model_aabb.m_max.y = mesh.m_original_aabb.m_max.y;
    }
    if (mesh.m_original_aabb.m_max.z > model_aabb.m_max.z) {
      model_aabb.m_max.z = mesh.m_original_aabb.m_max.z;
    }
  }
  m.m_aabb = model_aabb;

  std::string directory = path.substr(0, path.find_last_of('/') + 1);
  for (int i = 0; i < scene->mNumMaterials; i++) {
    auto *material = scene->mMaterials[i];
    MaterialEntry mat{};

    get_material_texture(directory, material, mat, aiTextureType_DIFFUSE,
                         TextureMapType::diffuse);
    get_material_texture(directory, material, mat, aiTextureType_NORMALS,
                         TextureMapType::normal);
    get_material_texture(directory, material, mat, aiTextureType_DISPLACEMENT,
                         TextureMapType::normal);
    get_material_texture(directory, material, mat, aiTextureType_SPECULAR,
                         TextureMapType::specular);
    get_material_texture(directory, material, mat,
                         aiTextureType_AMBIENT_OCCLUSION, TextureMapType::ao);
    get_material_texture(directory, material, mat,
                         aiTextureType_DIFFUSE_ROUGHNESS,
                         TextureMapType::roughness);
    get_material_texture(directory, material, mat, aiTextureType_METALNESS,
                         TextureMapType::metallicness);

    m.m_materials.push_back(mat);
  }

  return m;
}

Model Model::load_model_from_path_entries(
    const std::string &path, std::vector<TextureEntry> &texture_entries,
    std::vector<MeshEntry> &mesh_entries) {
  ZoneScoped;
  Assimp::Importer importer;
  const aiScene *scene = importer.ReadFile(
      path.c_str(), aiProcess_Triangulate | aiProcess_CalcTangentSpace |
                        aiProcess_OptimizeMeshes | aiProcess_GenSmoothNormals |
                        aiProcess_OptimizeGraph | aiProcess_FixInfacingNormals |
                        aiProcess_FindInvalidData | aiProcess_GenBoundingBoxes);
  //
  if (scene == nullptr) {
    return {};
  }

  Model m{};
  process_node(m, scene->mRootNode, scene, true, mesh_entries);

  m.update_aabb();

  std::string directory = path.substr(0, path.find_last_of('/') + 1);
  for (int i = 0; i < scene->mNumMaterials; i++) {
    auto *material = scene->mMaterials[i];
    MaterialEntry mat{};

    get_material_texture_entry(directory, material, mat, aiTextureType_DIFFUSE,
                               TextureMapType::diffuse, texture_entries);
    get_material_texture_entry(directory, material, mat, aiTextureType_NORMALS,
                               TextureMapType::normal, texture_entries);
    get_material_texture_entry(directory, material, mat,
                               aiTextureType_DISPLACEMENT,
                               TextureMapType::normal, texture_entries);
    get_material_texture_entry(directory, material, mat, aiTextureType_SPECULAR,
                               TextureMapType::specular, texture_entries);
    get_material_texture_entry(directory, material, mat,
                               aiTextureType_AMBIENT_OCCLUSION,
                               TextureMapType::ao, texture_entries);
    get_material_texture_entry(directory, material, mat,
                               aiTextureType_DIFFUSE_ROUGHNESS,
                               TextureMapType::roughness, texture_entries);
    get_material_texture_entry(directory, material, mat,
                               aiTextureType_METALNESS,
                               TextureMapType::metallicness, texture_entries);

    m.m_materials.push_back(mat);
  }

  return m;
}

void Model::update_aabb() {
  ZoneScoped;
  AABB model_aabb{};
  for (auto &mesh : m_meshes) {
    if (mesh.m_original_aabb.m_min.x < model_aabb.m_min.x) {
      model_aabb.m_min.x = mesh.m_original_aabb.m_min.x;
    }
    if (mesh.m_original_aabb.m_min.y < model_aabb.m_min.y) {
      model_aabb.m_min.y = mesh.m_original_aabb.m_min.y;
    }
    if (mesh.m_original_aabb.m_min.z < model_aabb.m_min.z) {
      model_aabb.m_min.z = mesh.m_original_aabb.m_min.z;
    }

    if (mesh.m_original_aabb.m_max.x > model_aabb.m_max.x) {
      model_aabb.m_max.x = mesh.m_original_aabb.m_max.x;
    }
    if (mesh.m_original_aabb.m_max.y > model_aabb.m_max.y) {
      model_aabb.m_max.y = mesh.m_original_aabb.m_max.y;
    }
    if (mesh.m_original_aabb.m_max.z > model_aabb.m_max.z) {
      model_aabb.m_max.z = mesh.m_original_aabb.m_max.z;
    }
  }
  m_aabb = model_aabb;
}

void Model::release() {
  ZoneScoped;
  for (Mesh &m : m_meshes) {
    m.m_vao.release();
  }
}
} // namespace gem