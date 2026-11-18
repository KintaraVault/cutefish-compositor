// SPDX-License-Identifier: BSD-3-Clause
#include "shell/spotlight/spotlight_preview.h"

#include <filesystem>

#include "cutefish/files/thumbnails/thumbnail_cache.h"

namespace cutefish::shell::spotlight {

ui::Bitmap loadFilePreview(const std::string &path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)))
        return {};
    return files::ThumbnailCache::self()->requestThumbnail(path, {112, 112}).image;
}

} // namespace cutefish::shell::spotlight
