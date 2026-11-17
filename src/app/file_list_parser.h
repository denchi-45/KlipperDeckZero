/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "gcode_files.h"
#include "jsonrpc_client.h"

#include <vector>

namespace app {

// Result of `server.files.list`, newest first. Entries without a path are skipped.
std::vector<GcodeFile> parse_file_list(const moonraker::Json& result);
// Result of `server.files.metadata`. Missing or null fields stay unknown.
GcodeMetadata parse_metadata(const moonraker::Json& result);

} // namespace app
