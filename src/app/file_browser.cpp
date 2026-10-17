/*
 * SPDX-License-Identifier: MIT
 */

#include "file_browser.h"

#include <algorithm>

namespace app {

void FileBrowser::begin_loading() {
    mode_ = Mode::Loading;
    error_.clear();
}

void FileBrowser::set_files(std::vector<GcodeFile> files) {
    const auto* previous = selected_file();
    const std::string previous_path = previous ? previous->path : std::string();

    files_ = std::move(files);
    mode_ = Mode::List;
    error_.clear();
    top_ = 0;
    selected_ = 0;
    const auto it = std::find_if(files_.begin(), files_.end(), [&](const GcodeFile& file) {
        return file.path == previous_path;
    });
    select(it == files_.end() ? 0 : static_cast<std::size_t>(it - files_.begin()));
}

void FileBrowser::set_failed(std::string error) {
    mode_ = Mode::Failed;
    error_ = std::move(error);
}

void FileBrowser::set_metadata(const std::string& path, const GcodeMetadata* metadata) {
    const auto* file = selected_file();
    if (mode_ != Mode::Details || !file || file->path != path) {
        return;
    }
    if (metadata) {
        metadata_ = *metadata;
        details_state_ = DetailsState::Loaded;
    }
    else {
        details_state_ = DetailsState::Unavailable;
    }
}

FileBrowser::Result FileBrowser::handle(PanelInput input) {
    // Digits stay page shortcuts in every mode.
    if (input == PanelInput::Digit) {
        return Result::Ignored;
    }

    switch (mode_) {
        case Mode::Loading:
            return input == PanelInput::Escape ? Result::Ignored : Result::Handled;
        case Mode::Failed:
            if (input == PanelInput::Enter) {
                begin_loading();
                return Result::Reload;
            }
            return input == PanelInput::Escape ? Result::Ignored : Result::Handled;
        case Mode::List: {
            const auto count = static_cast<int>(files_.size());
            const auto move = [&](int step) {
                if (count > 0) {
                    select(static_cast<std::size_t>(std::clamp(static_cast<int>(selected_) + step, 0, count - 1)));
                }
                return Result::Handled;
            };
            const auto page = static_cast<int>(kVisibleRows);
            switch (input) {
                case PanelInput::Up:
                    return move(-1);
                case PanelInput::Down:
                    return move(1);
                case PanelInput::Left:
                    return move(-page);
                case PanelInput::Right:
                    return move(page);
                case PanelInput::Enter:
                    if (files_.empty()) {
                        return Result::Handled;
                    }
                    mode_ = Mode::Details;
                    details_state_ = DetailsState::Loading;
                    metadata_ = {};
                    return Result::OpenDetails;
                default:
                    return Result::Ignored;
            }
        }
        case Mode::Details:
            if (input == PanelInput::Enter) {
                return Result::StartPrint;
            }
            if (input == PanelInput::Escape || input == PanelInput::Backspace) {
                mode_ = Mode::List;
            }
            return Result::Handled;
    }
    return Result::Ignored;
}

FileBrowser::Mode FileBrowser::mode() const {
    return mode_;
}

const std::vector<GcodeFile>& FileBrowser::files() const {
    return files_;
}

std::size_t FileBrowser::selected() const {
    return selected_;
}

std::size_t FileBrowser::top() const {
    return top_;
}

const GcodeFile* FileBrowser::selected_file() const {
    return selected_ < files_.size() ? &files_[selected_] : nullptr;
}

FileBrowser::DetailsState FileBrowser::details_state() const {
    return details_state_;
}

const GcodeMetadata& FileBrowser::metadata() const {
    return metadata_;
}

const std::string& FileBrowser::error() const {
    return error_;
}

void FileBrowser::select(std::size_t index) {
    selected_ = index;
    if (selected_ < top_) {
        top_ = selected_;
    }
    else if (selected_ >= top_ + kVisibleRows) {
        top_ = selected_ + 1 - kVisibleRows;
    }
}

} // namespace app
