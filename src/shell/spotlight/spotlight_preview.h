// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <string>

#include "ui/bitmap.h"

namespace cutefish::shell::spotlight {

// To be called on a worker. Null means the caller keeps the file-type icon.
ui::Bitmap loadFilePreview(const std::string &path);

} // namespace cutefish::shell::spotlight
