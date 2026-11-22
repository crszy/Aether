#include "region_finder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

#include <qnamespace.h>
#include <qtypes.h>

namespace qs::windows::image {

namespace {

constexpr int kWorkingMaxDim = 720;
constexpr double kIouThreshold = 0.7;
constexpr double kFullImageAreaRatio = 0.92;

struct WorkImage {
	int w = 0;
	int h = 0;
	std::vector<std::uint8_t> gray;
};

WorkImage buildWorkingImage(const QImage& image, double& scaleX, double& scaleY) {
	auto grayFull = image.convertToFormat(QImage::Format_Grayscale8);
	auto origW = grayFull.width();
	auto origH = grayFull.height();

	auto longest = std::max(origW, origH);
	auto factor = longest > kWorkingMaxDim ? static_cast<double>(kWorkingMaxDim) / longest : 1.0;

	auto workW = std::max(1, static_cast<int>(std::lround(origW * factor)));
	auto workH = std::max(1, static_cast<int>(std::lround(origH * factor)));

	auto scaled = factor < 1.0
	    ? grayFull.scaled(workW, workH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
	    : grayFull;

	WorkImage work;
	work.w = scaled.width();
	work.h = scaled.height();
	work.gray.resize(static_cast<std::size_t>(work.w) * static_cast<std::size_t>(work.h));
	for (int y = 0; y < work.h; y++) {
		const auto* line = scaled.constScanLine(y);
		std::copy(line, line + work.w, work.gray.begin() + static_cast<std::ptrdiff_t>(y) * work.w);
	}

	scaleX = static_cast<double>(origW) / work.w;
	scaleY = static_cast<double>(origH) / work.h;
	return work;
}

std::vector<int> sobelMagnitude(const WorkImage& img) {
	auto w = img.w;
	auto h = img.h;
	std::vector<int> mag(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);

	auto at = [&](int x, int y) {
		x = std::clamp(x, 0, w - 1);
		y = std::clamp(y, 0, h - 1);
		return static_cast<int>(img.gray[static_cast<std::size_t>(y) * w + x]);
	};

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			auto gx = (at(x + 1, y - 1) + 2 * at(x + 1, y) + at(x + 1, y + 1))
			        - (at(x - 1, y - 1) + 2 * at(x - 1, y) + at(x - 1, y + 1));
			auto gy = (at(x - 1, y + 1) + 2 * at(x, y + 1) + at(x + 1, y + 1))
			        - (at(x - 1, y - 1) + 2 * at(x, y - 1) + at(x + 1, y - 1));
			auto m = (std::abs(gx) + std::abs(gy)) / 8;
			mag[static_cast<std::size_t>(y) * w + x] = std::min(255, m);
		}
	}

	return mag;
}

int otsuThreshold(const std::vector<int>& mag) {
	std::array<int, 256> hist {};
	for (auto v : mag) hist[static_cast<std::size_t>(std::clamp(v, 0, 255))]++;

	auto total = static_cast<double>(mag.size());
	if (total <= 0) return 0;

	double sum = 0;
	for (int i = 0; i < 256; i++) sum += static_cast<double>(i) * hist[static_cast<std::size_t>(i)];

	double sumB = 0;
	double wB = 0;
	double maxVar = 0;
	int threshold = 0;

	for (int t = 0; t < 256; t++) {
		wB += hist[static_cast<std::size_t>(t)];
		if (wB <= 0) continue;

		auto wF = total - wB;
		if (wF <= 0) break;

		sumB += static_cast<double>(t) * hist[static_cast<std::size_t>(t)];
		auto mB = sumB / wB;
		auto mF = (sum - sumB) / wF;
		auto varBetween = wB * wF * (mB - mF) * (mB - mF);
		if (varBetween > maxVar) {
			maxVar = varBetween;
			threshold = t;
		}
	}

	return threshold;
}

std::vector<std::uint8_t> thresholdMask(const std::vector<int>& mag, int threshold) {
	std::vector<std::uint8_t> mask(mag.size());
	auto cutoff = std::max(threshold, 8);
	for (std::size_t i = 0; i < mag.size(); i++) mask[i] = mag[i] > cutoff ? 1 : 0;
	return mask;
}

std::vector<std::uint8_t> dilate(const std::vector<std::uint8_t>& mask, int w, int h, int radius) {
	if (radius <= 0) return mask;

	std::vector<std::uint8_t> tmp(mask.size());
	for (int y = 0; y < h; y++) {
		auto row = static_cast<std::size_t>(y) * w;
		for (int x = 0; x < w; x++) {
			std::uint8_t v = 0;
			auto x0 = std::max(0, x - radius);
			auto x1 = std::min(w - 1, x + radius);
			for (int xx = x0; xx <= x1 && !v; xx++) v = mask[row + static_cast<std::size_t>(xx)];
			tmp[row + static_cast<std::size_t>(x)] = v;
		}
	}

	std::vector<std::uint8_t> out(mask.size());
	for (int x = 0; x < w; x++) {
		for (int y = 0; y < h; y++) {
			std::uint8_t v = 0;
			auto y0 = std::max(0, y - radius);
			auto y1 = std::min(h - 1, y + radius);
			for (int yy = y0; yy <= y1 && !v; yy++) v = tmp[static_cast<std::size_t>(yy) * w + x];
			out[static_cast<std::size_t>(y) * w + x] = v;
		}
	}

	return out;
}

struct Box {
	int minX;
	int minY;
	int maxX;
	int maxY;
};

std::vector<Box> connectedComponents(const std::vector<std::uint8_t>& mask, int w, int h) {
	std::vector<std::uint8_t> visited(mask.size(), 0);
	std::vector<int> stack;
	std::vector<Box> boxes;

	static constexpr std::array<int, 8> kDx = {-1, 0, 1, -1, 1, -1, 0, 1};
	static constexpr std::array<int, 8> kDy = {-1, -1, -1, 0, 0, 1, 1, 1};

	for (int start = 0; start < static_cast<int>(mask.size()); start++) {
		if (!mask[static_cast<std::size_t>(start)] || visited[static_cast<std::size_t>(start)])
			continue;

		auto sx = start % w;
		auto sy = start / w;
		Box box {sx, sy, sx, sy};

		visited[static_cast<std::size_t>(start)] = 1;
		stack.push_back(start);

		while (!stack.empty()) {
			auto cur = stack.back();
			stack.pop_back();
			auto cx = cur % w;
			auto cy = cur / w;

			box.minX = std::min(box.minX, cx);
			box.maxX = std::max(box.maxX, cx);
			box.minY = std::min(box.minY, cy);
			box.maxY = std::max(box.maxY, cy);

			for (int d = 0; d < 8; d++) {
				auto nx = cx + kDx[static_cast<std::size_t>(d)];
				auto ny = cy + kDy[static_cast<std::size_t>(d)];
				if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;

				auto nidx = ny * w + nx;
				if (!mask[static_cast<std::size_t>(nidx)] || visited[static_cast<std::size_t>(nidx)])
					continue;

				visited[static_cast<std::size_t>(nidx)] = 1;
				stack.push_back(nidx);
			}
		}

		boxes.push_back(box);
	}

	return boxes;
}

double iou(const QRect& a, const QRect& b) {
	auto inter = a.intersected(b);
	if (inter.isEmpty()) return 0.0;

	auto interArea = static_cast<double>(inter.width()) * inter.height();
	auto areaA = static_cast<double>(a.width()) * a.height();
	auto areaB = static_cast<double>(b.width()) * b.height();
	auto unionArea = areaA + areaB - interArea;
	return unionArea > 0 ? interArea / unionArea : 0.0;
}

std::vector<QRect> nonMaxSuppression(std::vector<QRect> regions) {
	std::sort(regions.begin(), regions.end(), [](const QRect& a, const QRect& b) {
		return (static_cast<qint64>(a.width()) * a.height())
		     > (static_cast<qint64>(b.width()) * b.height());
	});

	std::vector<QRect> kept;
	std::vector<bool> removed(regions.size(), false);

	for (std::size_t i = 0; i < regions.size(); i++) {
		if (removed[i]) continue;
		kept.push_back(regions[i]);

		for (std::size_t j = i + 1; j < regions.size(); j++) {
			if (removed[j]) continue;
			if (iou(regions[i], regions[j]) >= kIouThreshold) removed[j] = true;
		}
	}

	return kept;
}

} // namespace

std::vector<QRect> detectContentRegions(const QImage& image, const RegionFinderOptions& options) {
	if (image.isNull() || image.width() <= 0 || image.height() <= 0) return {};

	double scaleX = 1.0;
	double scaleY = 1.0;
	auto work = buildWorkingImage(image, scaleX, scaleY);
	if (work.w <= 2 || work.h <= 2) return {};

	auto mag = sobelMagnitude(work);
	auto threshold = otsuThreshold(mag);
	auto baseMask = thresholdMask(mag, threshold);

	auto origW = image.width();
	auto origH = image.height();

	static constexpr std::array<int, 4> kRadiusDivisors = {160, 80, 40, 20};

	std::vector<QRect> candidates;

	for (auto divisor : kRadiusDivisors) {
		auto radius = std::clamp(std::min(work.w, work.h) / divisor, 1, 24);
		auto mask = dilate(baseMask, work.w, work.h, radius);
		auto boxes = connectedComponents(mask, work.w, work.h);

		for (const auto& box : boxes) {
			auto x = static_cast<int>(std::floor(box.minX * scaleX));
			auto y = static_cast<int>(std::floor(box.minY * scaleY));
			auto right = static_cast<int>(std::ceil((box.maxX + 1) * scaleX));
			auto bottom = static_cast<int>(std::ceil((box.maxY + 1) * scaleY));

			x = std::clamp(x, 0, origW);
			y = std::clamp(y, 0, origH);
			right = std::clamp(right, 0, origW);
			bottom = std::clamp(bottom, 0, origH);

			auto width = right - x;
			auto height = bottom - y;
			if (width <= 0 || height <= 0) continue;

			if (width >= origW * kFullImageAreaRatio && height >= origH * kFullImageAreaRatio)
				continue;

			if (!(width > options.minWidth && height > options.minHeight)) continue;
			if (options.maxWidth > 0 && width >= options.maxWidth) continue;
			if (options.maxHeight > 0 && height >= options.maxHeight) continue;

			candidates.push_back(QRect(x, y, width, height));
		}
	}

	return nonMaxSuppression(std::move(candidates));
}

} // namespace qs::windows::image
