#include "image_tools.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

#include <qcolor.h>
#include <qdatetime.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qimage.h>
#include <qimageiohandler.h>
#include <qimagereader.h>
#include <qlist.h>
#include <qloggingcategory.h>
#include <qmutex.h>
#include <qpoint.h>
#include <qrect.h>
#include <qrgb.h>
#include <qsize.h>
#include <qvariant.h>

#include "image_tools_backend.hpp"
#include "region_finder.hpp"

namespace qs::windows::image {

namespace {
Q_LOGGING_CATEGORY(logImageTools, "quickshell.windows.imagetools", QtWarningMsg);

constexpr int SCHEME_SAMPLE_DIM = 128;
constexpr qsizetype MAX_CACHED_RESULTS = 32;

QString toHex(int r, int g, int b) {
	return QColor(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255)).name();
}

class ResultCache {
public:
	std::optional<QVariant> find(const QString& key) {
		if (key.isEmpty()) return std::nullopt;

		QMutexLocker locker(&this->mutex);
		auto it = this->values.constFind(key);
		if (it == this->values.constEnd()) return std::nullopt;
		return it.value();
	}

	void insert(const QString& key, const QVariant& value) {
		if (key.isEmpty()) return;

		QMutexLocker locker(&this->mutex);
		if (this->values.contains(key)) return;

		this->values.insert(key, value);
		this->order.append(key);
		if (this->order.size() > MAX_CACHED_RESULTS) this->values.remove(this->order.takeFirst());
	}

private:
	QMutex mutex;
	QHash<QString, QVariant> values;
	QList<QString> order;
};

ResultCache& schemeCache() {
	static auto* cache = new ResultCache(); // NOLINT
	return *cache;
}

ResultCache& textColorCache() {
	static auto* cache = new ResultCache(); // NOLINT
	return *cache;
}

QString fileCacheKey(const QString& path) {
	auto info = QFileInfo(path);
	if (!info.isFile()) return QString();

	return path + u'|' + QString::number(info.lastModified().toMSecsSinceEpoch()) + u'|'
	     + QString::number(info.size());
}

QSize fitWithin(const QSize& size, int maxDim) {
	auto scale = static_cast<double>(maxDim) / std::max(size.width(), size.height());
	auto newW = std::max(1, static_cast<int>(size.width() * scale));
	auto newH = std::max(1, static_cast<int>(size.height() * scale));
	return {newW, newH};
}

QImage readDownscaled(const QString& path, int maxDim) {
	auto reader = QImageReader(path);
	auto size = reader.size();
	auto prescaled = size.isValid() && std::max(size.width(), size.height()) > maxDim;
	if (prescaled) reader.setScaledSize(fitWithin(size, maxDim));

	auto image = reader.read();
	if (image.isNull()) return image;

	image = image.convertToFormat(QImage::Format_RGB32);
	if (!prescaled && std::max(image.width(), image.height()) > maxDim) {
		image = image.scaled(
		    fitWithin(image.size(), maxDim),
		    Qt::IgnoreAspectRatio,
		    Qt::SmoothTransformation
		);
	}

	return image;
}

QImage readForScreen(const QString& path, int targetW, int targetH) {
	auto reader = QImageReader(path);

	if (targetW > 0 && targetH > 0) {
		auto size = reader.size();
		auto rotated = reader.autoTransform()
		            && reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
		if (rotated) size.transpose();

		if (size.isValid() && !size.isEmpty()) {
			auto scale = std::max(
			    static_cast<double>(targetW) / size.width(),
			    static_cast<double>(targetH) / size.height()
			);

			if (scale < 1.0) {
				auto scaled = QSize(
				    std::max(1, static_cast<int>(std::lround(size.width() * scale))),
				    std::max(1, static_cast<int>(std::lround(size.height() * scale)))
				);
				if (rotated) scaled.transpose();
				reader.setScaledSize(scaled);
			}
		}
	}

	return reader.read();
}

QImage scaleAndCropToScreen(const QImage& src, int targetW, int targetH) {
	if (targetW <= 0 || targetH <= 0 || src.isNull()) return src;

	auto scaleW = static_cast<double>(targetW) / src.width();
	auto scaleH = static_cast<double>(targetH) / src.height();
	auto scale = std::max(scaleW, scaleH);

	auto newW = std::max(1, static_cast<int>(std::lround(src.width() * scale)));
	auto newH = std::max(1, static_cast<int>(std::lround(src.height() * scale)));

	auto scaled = src.scaled(newW, newH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

	auto x1 = std::max(0, (newW - targetW) / 2);
	auto y1 = std::max(0, (newH - targetH) / 2);
	auto w = std::min(targetW, newW);
	auto h = std::min(targetH, newH);

	return scaled.copy(x1, y1, w, h);
}

struct IntegralImage {
	int w = 0;
	int h = 0;
	std::vector<double> sum;
	std::vector<double> sumSq;

	IntegralImage(const QImage& gray): w(gray.width()), h(gray.height()) {
		auto cellCount = static_cast<size_t>(w) * h;
		this->sum.assign(cellCount, 0.0);
		this->sumSq.assign(cellCount, 0.0);

		for (int y = 0; y < h; y++) {
			const auto* line = gray.constScanLine(y);
			for (int x = 0; x < w; x++) {
				double v = line[x];
				double left = x > 0 ? this->sum[idx(x - 1, y)] : 0.0;
				double up = y > 0 ? this->sum[idx(x, y - 1)] : 0.0;
				double upLeft = (x > 0 && y > 0) ? this->sum[idx(x - 1, y - 1)] : 0.0;
				this->sum[idx(x, y)] = v + left + up - upLeft;

				double leftSq = x > 0 ? this->sumSq[idx(x - 1, y)] : 0.0;
				double upSq = y > 0 ? this->sumSq[idx(x, y - 1)] : 0.0;
				double upLeftSq = (x > 0 && y > 0) ? this->sumSq[idx(x - 1, y - 1)] : 0.0;
				this->sumSq[idx(x, y)] = v * v + leftSq + upSq - upLeftSq;
			}
		}
	}

	[[nodiscard]] size_t idx(int x, int y) const { return static_cast<size_t>(y) * w + x; }

	[[nodiscard]] double regionSum(const std::vector<double>& ii, int x1, int y1, int x2, int y2)
	    const {
		double total = ii[idx(x2, y2)];
		if (x1 > 0) total -= ii[idx(x1 - 1, y2)];
		if (y1 > 0) total -= ii[idx(x2, y1 - 1)];
		if (x1 > 0 && y1 > 0) total += ii[idx(x1 - 1, y1 - 1)];
		return total;
	}
};

QString dominantColorOfRegion(const QImage& color, const QRect& rect) {
	auto clamped = rect.intersected(color.rect());
	if (clamped.isEmpty()) return toHex(0, 0, 0);

	long long sumR = 0, sumG = 0, sumB = 0;
	long long count = 0;

	for (int y = clamped.top(); y <= clamped.bottom(); y++) {
		for (int x = clamped.left(); x <= clamped.right(); x++) {
			auto pixel = color.pixelColor(x, y);
			if (pixel.red() <= 10 && pixel.green() <= 10 && pixel.blue() <= 10) continue;
			sumR += pixel.red();
			sumG += pixel.green();
			sumB += pixel.blue();
			count++;
		}
	}

	if (count == 0) {
		for (int y = clamped.top(); y <= clamped.bottom(); y++) {
			for (int x = clamped.left(); x <= clamped.right(); x++) {
				auto pixel = color.pixelColor(x, y);
				sumR += pixel.red();
				sumG += pixel.green();
				sumB += pixel.blue();
				count++;
			}
		}
	}

	if (count == 0) return toHex(0, 0, 0);
	return toHex(
	    static_cast<int>(sumR / count),
	    static_cast<int>(sumG / count),
	    static_cast<int>(sumB / count)
	);
}

} // namespace

ImageTools::ImageTools(QObject* parent)
    : QObject(parent)
    , mBackend(std::make_unique<ImageToolsBackend>(this)) {}

ImageTools::~ImageTools() = default;

QVariantMap ImageTools::leastBusyRegion(
    const QString& imagePath,
    int width,
    int height,
    int screenWidth,
    int screenHeight,
    int horizontalPadding,
    int verticalPadding,
    bool busiest
) {
	if (imagePath.isEmpty()) return QVariantMap {{"error", QStringLiteral("No image")}};

	auto original = readForScreen(imagePath, screenWidth, screenHeight);
	if (original.isNull()) {
		qCWarning(logImageTools) << "leastBusyRegion: could not load" << imagePath;
		return QVariantMap {{"error", QStringLiteral("Image not found")}};
	}

	original = original.convertToFormat(QImage::Format_RGB32);
	auto working = scaleAndCropToScreen(original, screenWidth, screenHeight);
	auto gray = working.convertToFormat(QImage::Format_Grayscale8);

	auto w = gray.width();
	auto h = gray.height();
	if (w <= 0 || h <= 0) return QVariantMap {{"error", QStringLiteral("Image too small")}};

	if (horizontalPadding * 2 >= w || verticalPadding * 2 >= h) {
		horizontalPadding = std::max(0, std::min(horizontalPadding, (w - 1) / 2));
		verticalPadding = std::max(0, std::min(verticalPadding, (h - 1) / 2));
	}

	auto maxRegionW = w - 2 * horizontalPadding;
	auto maxRegionH = h - 2 * verticalPadding;
	if (maxRegionW <= 0 || maxRegionH <= 0) {
		return QVariantMap {{"error", QStringLiteral("Image too small for the specified padding")}};
	}

	width = std::min(width, maxRegionW);
	height = std::min(height, maxRegionH);

	const IntegralImage integral(gray);

	const int stride = 10;
	double area = static_cast<double>(width) * height;

	std::optional<double> bestVar;
	QPoint bestCoords(horizontalPadding, verticalPadding);

	int xStart = horizontalPadding;
	int yStart = verticalPadding;
	int xEnd = std::max(xStart, w - width - horizontalPadding);
	int yEnd = std::max(yStart, h - height - verticalPadding);

	for (int y = yStart; y <= yEnd; y += stride) {
		for (int x = xStart; x <= xEnd; x += stride) {
			int x2 = x + width - 1;
			int y2 = y + height - 1;
			if (x2 >= w || y2 >= h) continue;

			auto s = integral.regionSum(integral.sum, x, y, x2, y2);
			auto sSq = integral.regionSum(integral.sumSq, x, y, x2, y2);
			auto mean = s / area;
			auto var = (sSq / area) - (mean * mean);

			if (!bestVar.has_value() || (busiest ? var > *bestVar : var < *bestVar)) {
				bestVar = var;
				bestCoords = QPoint(x, y);
			}
		}
	}

	auto centerX = bestCoords.x() + width / 2;
	auto centerY = bestCoords.y() + height / 2;
	auto dominant =
	    dominantColorOfRegion(working, QRect(bestCoords.x(), bestCoords.y(), width, height));

	QVariantMap result;
	result["center_x"] = centerX;
	result["center_y"] = centerY;
	result["width"] = width;
	result["height"] = height;
	result["variance"] = bestVar.value_or(0.0);
	result["dominant_color"] = dominant;
	return result;
}

QVariantMap ImageTools::textColorFromImage(const QString& imagePath) {
	auto cacheKey = fileCacheKey(imagePath);
	if (auto cached = textColorCache().find(cacheKey)) return cached->toMap();

	auto img = QImageReader(imagePath).read();
	if (img.isNull()) {
		qCWarning(logImageTools) << "textColorFromImage: could not load" << imagePath;
		return QVariantMap {{"error", QStringLiteral("Could not decode image data")}};
	}

	img = img.convertToFormat(QImage::Format_RGB32);
	auto w = img.width();
	auto h = img.height();
	if (w <= 0 || h <= 0) return QVariantMap {{"error", QStringLiteral("Empty image")}};

	std::array<QColor, 4> corners = {
	    img.pixelColor(0, 0),
	    img.pixelColor(w - 1, 0),
	    img.pixelColor(0, h - 1),
	    img.pixelColor(w - 1, h - 1),
	};
	std::array<int, 4> rs, gs, bs;
	for (int i = 0; i < 4; i++) {
		rs[i] = corners[i].red();
		gs[i] = corners[i].green();
		bs[i] = corners[i].blue();
	}
	std::sort(rs.begin(), rs.end());
	std::sort(gs.begin(), gs.end());
	std::sort(bs.begin(), bs.end());
	auto bgR = (rs[1] + rs[2]) / 2.0;
	auto bgG = (gs[1] + gs[2]) / 2.0;
	auto bgB = (bs[1] + bs[2]) / 2.0;

	std::vector<double> distances;
	distances.reserve(static_cast<size_t>(w) * h);

	for (int y = 0; y < h; y++) {
		const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; x++) {
			auto px = line[x];
			double dr = qRed(px) - bgR, dg = qGreen(px) - bgG, db = qBlue(px) - bgB;
			distances.push_back(std::sqrt(dr * dr + dg * dg + db * db));
		}
	}

	std::vector<double> ranked = distances;
	double rank = 0.95 * (static_cast<double>(ranked.size()) - 1);
	auto lo = static_cast<size_t>(std::floor(rank));
	auto hi = static_cast<size_t>(std::ceil(rank));
	auto loIt = ranked.begin() + static_cast<std::ptrdiff_t>(lo);
	std::nth_element(ranked.begin(), loIt, ranked.end());
	auto loValue = *loIt;
	auto hiValue = hi == lo ? loValue : *std::min_element(loIt + 1, ranked.end());
	double threshold = loValue + (hiValue - loValue) * (rank - static_cast<double>(lo));

	std::vector<int> tr, tg, tb;
	size_t index = 0;
	for (int y = 0; y < h; y++) {
		const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; x++, index++) {
			if (distances[index] < threshold) continue;
			tr.push_back(qRed(line[x]));
			tg.push_back(qGreen(line[x]));
			tb.push_back(qBlue(line[x]));
		}
	}

	int textR = 255, textG = 255, textB = 255;
	if (!tr.empty()) {
		auto median = [](std::vector<int> v) {
			auto n = v.size();
			auto mid = v.begin() + static_cast<std::ptrdiff_t>(n / 2);
			std::nth_element(v.begin(), mid, v.end());
			if (n % 2 != 0) return *mid;
			return (*std::max_element(v.begin(), mid) + *mid) / 2;
		};
		textR = median(tr);
		textG = median(tg);
		textB = median(tb);
	}

	QVariantMap result;
	result["background"] = toHex(static_cast<int>(bgR), static_cast<int>(bgG), static_cast<int>(bgB));
	result["text"] = toHex(textR, textG, textB);
	textColorCache().insert(cacheKey, result);
	return result;
}

QString ImageTools::schemeForImage(const QString& imagePath) {
	auto cacheKey = fileCacheKey(imagePath);
	if (auto cached = schemeCache().find(cacheKey)) return cached->toString();

	auto img = readDownscaled(imagePath, SCHEME_SAMPLE_DIM);
	if (img.isNull()) {
		qCWarning(logImageTools) << "schemeForImage: could not load" << imagePath;
		return QStringLiteral("scheme-tonal-spot");
	}

	auto w = img.width();
	auto h = img.height();
	if (w <= 0 || h <= 0) return QStringLiteral("scheme-tonal-spot");

	double sumRg = 0, sumYb = 0, sumRgSq = 0, sumYbSq = 0;
	auto n = static_cast<double>(w) * h;

	for (int y = 0; y < h; y++) {
		const auto* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
		for (int x = 0; x < w; x++) {
			auto px = line[x];
			double r = qRed(px), g = qGreen(px), b = qBlue(px);
			double rg = std::abs(r - g);
			double yb = std::abs(0.5 * (r + g) - b);
			sumRg += rg;
			sumYb += yb;
			sumRgSq += rg * rg;
			sumYbSq += yb * yb;
		}
	}

	auto meanRg = sumRg / n;
	auto meanYb = sumYb / n;
	auto varRg = (sumRgSq / n) - (meanRg * meanRg);
	auto varYb = (sumYbSq / n) - (meanYb * meanYb);
	auto stdRg = std::sqrt(std::max(0.0, varRg));
	auto stdYb = std::sqrt(std::max(0.0, varYb));

	auto colorfulness = std::sqrt(stdRg * stdRg + stdYb * stdYb)
	                   + (0.3 * std::sqrt(meanRg * meanRg + meanYb * meanYb));

	auto scheme =
	    colorfulness < 40 ? QStringLiteral("scheme-neutral") : QStringLiteral("scheme-tonal-spot");
	schemeCache().insert(cacheKey, scheme);
	return scheme;
}

QSize ImageTools::imageSize(const QString& imagePath) {
	QImageReader reader(imagePath);
	reader.setAutoTransform(true);
	auto size = reader.size();
	if (!size.isValid()) return {};

	if (reader.transformation().testFlag(QImageIOHandler::TransformationRotate90)) size.transpose();
	return size;
}

int ImageTools::requestLeastBusyRegion(
    const QString& imagePath,
    int width,
    int height,
    int screenWidth,
    int screenHeight,
    int horizontalPadding,
    int verticalPadding,
    bool busiest
) {
	auto requestId = this->mNextRequestId++;
	this->mBackend->requestLeastBusyRegion(
	    requestId,
	    imagePath,
	    width,
	    height,
	    screenWidth,
	    screenHeight,
	    horizontalPadding,
	    verticalPadding,
	    busiest
	);
	return requestId;
}

int ImageTools::requestSchemeForImage(const QString& imagePath) {
	auto requestId = this->mNextRequestId++;
	this->mBackend->requestScheme(requestId, imagePath);
	return requestId;
}

QVariantList ImageTools::findContentRegions(
    const QString& imagePath,
    int minWidth,
    int minHeight,
    int maxWidth,
    int maxHeight
) {
	auto image = QImageReader(imagePath).read();
	if (image.isNull()) {
		qCWarning(logImageTools) << "findContentRegions: could not load" << imagePath;
		return {};
	}

	RegionFinderOptions options;
	options.minWidth = minWidth;
	options.minHeight = minHeight;
	options.maxWidth = maxWidth;
	options.maxHeight = maxHeight;

	auto boxes = detectContentRegions(image, options);

	QVariantList regions;
	regions.reserve(boxes.size());
	for (const auto& box : boxes) {
		QVariantMap region;
		region["at"] = QVariantList {box.x(), box.y()};
		region["size"] = QVariantList {box.width(), box.height()};
		regions.append(region);
	}
	return regions;
}

int ImageTools::requestFindContentRegions(
    const QString& imagePath,
    int minWidth,
    int minHeight,
    int maxWidth,
    int maxHeight
) {
	auto requestId = this->mNextRequestId++;
	this->mBackend->requestFindContentRegions(requestId, imagePath, minWidth, minHeight, maxWidth, maxHeight);
	return requestId;
}

void ImageTools::backendDone(int requestId, const QVariantMap& result) {
	emit this->leastBusyRegionReady(requestId, result);
}

void ImageTools::backendSchemeDone(int requestId, const QString& scheme) {
	emit this->schemeForImageReady(requestId, scheme);
}

void ImageTools::backendFindContentRegionsDone(int requestId, const QVariantList& regions) {
	emit this->findContentRegionsReady(requestId, regions);
}

} // namespace qs::windows::image
