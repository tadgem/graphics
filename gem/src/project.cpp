#include "gem/project.h"
#include "gem/asset_manager.h"
#include "gem/profile.h"

namespace gem {

nlohmann::json project::serialize(asset_manager &am) {
  ZoneScoped;
  nlohmann::json json{};

  json["project_name"] = m_name;
  json["project_assets"] = m_project_assets;
  json["scenes"] = m_scene_paths;

  return json;
}

void project::deserialize(asset_manager &am, nlohmann::json &proj_json) {
  ZoneScoped;
  m_name = proj_json["project_name"];
  m_project_assets = proj_json["project_assets"];
  m_scene_paths = proj_json["scenes"];
}
} // namespace gem