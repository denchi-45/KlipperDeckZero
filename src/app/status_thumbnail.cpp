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
    thumbnail_.clear();
    return file_.empty() ? Action::Clear : Action::Fetch;
}

const std::string& StatusThumbnail::file() const {
    return file_;
}

bool StatusThumbnail::wants(const std::string& path) const {
    return !path.empty() && path == file_;
}

void StatusThumbnail::set_thumbnail(const std::string& path, std::string png) {
    if (wants(path)) {
        thumbnail_ = std::move(png);
    }
}

const std::string& StatusThumbnail::thumbnail() const {
    return thumbnail_;
}

} // namespace app
