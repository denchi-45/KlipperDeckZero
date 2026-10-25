/*
 * SPDX-License-Identifier: MIT
 */

#include "status_thumbnail.h"

#include "control_panel.h"

#include <utility>

namespace app {

StatusThumbnail::Action StatusThumbnail::update(model::PrintState state, const std::string& filename) {
    const std::string wanted = has_active_print(state) ? filename : std::string();
    if (wanted == file_) {
        return Action::None;
    }
    file_ = wanted;
    ++generation_;
    thumbnail_.clear();
    return file_.empty() ? Action::Clear : Action::Fetch;
}

const std::string& StatusThumbnail::file() const {
    return file_;
}

std::uint32_t StatusThumbnail::generation() const {
    return generation_;
}

bool StatusThumbnail::wants(std::uint32_t generation) const {
    return !file_.empty() && generation == generation_;
}

void StatusThumbnail::set_thumbnail(std::uint32_t generation, std::string png) {
    if (wants(generation)) {
        thumbnail_ = std::move(png);
    }
}

const std::string& StatusThumbnail::thumbnail() const {
    return thumbnail_;
}

} // namespace app
