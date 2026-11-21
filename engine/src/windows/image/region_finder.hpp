#pragma once

#include <vector>

#include <qimage.h>
#include <qrect.h>

namespace qs::windows::image {

struct RegionFinderOptions {
	int minWidth = 200;
	int minHeight = 100;
	int maxWidth = 0;
	int maxHeight = 0;
};

std::vector<QRect> detectContentRegions(const QImage& image, const RegionFinderOptions& options);

} // namespace qs::windows::image
