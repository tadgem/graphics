#pragma once
#include "gem/shader.h"

namespace gem {
class gl_framebuffer;

namespace tech {
class taa {
public:
  static void dispatch_taa_pass(shader &taa, gl_framebuffer &pass_buffer,
                                gl_framebuffer pass_resolve_buffer,
                                gl_framebuffer &pass_history_buffer,
                                gl_handle &velocity_buffer_attachment,
                                glm::ivec2 window_res);
};
} // namespace tech
} // namespace gem