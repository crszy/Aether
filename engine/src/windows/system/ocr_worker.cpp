#include "ocr_worker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <qcolor.h>
#include <qcoreapplication.h>
#include <qdir.h>
#include <qfileinfo.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmath.h>
#include <qmetaobject.h>
#include <qrect.h>
#include <qstring.h>
#include <qvariant.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

#include "ocr.hpp"

using namespace winrt::Windows::Media::Ocr;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage;

namespace qs::windows::sys {

namespace {
Q_LOGGING_CATEGORY(logOcrWorker, "quickshell.windows.ocr", QtWarningMsg);

QString hresultError(const winrt::hresult_error& e) {
	return QStringLiteral("0x%1").arg(static_cast<uint32_t>(e.code().value), 8, 16, QLatin1Char('0'));
}

bool isCjk(char32_t c) {
	return (c >= 0x2E80 && c <= 0x2FDF) || (c >= 0x3000 && c <= 0x30FF) || (c >= 0x31C0 && c <= 0x31FF)
	    || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF)
	    || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x3FFFF);
}

char32_t firstCodePoint(const QString& s) {
	if (s.isEmpty()) return 0;
	if (s.size() > 1 && s.at(0).isHighSurrogate() && s.at(1).isLowSurrogate()) {
		return QChar::surrogateToUcs4(s.at(0), s.at(1));
	}
	return s.at(0).unicode();
}

char32_t lastCodePoint(const QString& s) {
	auto n = s.size();
	if (n == 0) return 0;
	if (n > 1 && s.at(n - 1).isLowSurrogate() && s.at(n - 2).isHighSurrogate()) {
		return QChar::surrogateToUcs4(s.at(n - 2), s.at(n - 1));
	}
	return s.at(n - 1).unicode();
}

class LineColorSampler {
public:
	LineColorSampler(const uint8_t* pixels, int width, int height, int stride, double angle)
	    : mPixels(pixels)
	    , mWidth(width)
	    , mHeight(height)
	    , mStride(stride)
	    , mCenterX(width / 2.0)
	    , mCenterY(height / 2.0)
	    , mCos(std::cos(qDegreesToRadians(angle)))
	    , mSin(std::sin(qDegreesToRadians(angle)))
	    , mCounts(BucketCount)
	    , mSumR(BucketCount)
	    , mSumG(BucketCount)
	    , mSumB(BucketCount) {}

	std::pair<QString, QString> colors(const QRectF& rect) {
		std::ranges::fill(this->mCounts, 0);
		std::ranges::fill(this->mSumR, 0);
		std::ranges::fill(this->mSumG, 0);
		std::ranges::fill(this->mSumB, 0);

		auto area = rect.width() * rect.height();
		auto step = std::max(1.0, std::ceil(std::sqrt(area / MaxSamples)));
		uint32_t total = 0;

		for (auto v = rect.top() + step / 2; v < rect.bottom(); v += step) {
			for (auto u = rect.left() + step / 2; u < rect.right(); u += step) {
				auto dx = u - this->mCenterX;
				auto dy = v - this->mCenterY;
				auto px = static_cast<int>(std::floor(this->mCenterX + dx * this->mCos - dy * this->mSin));
				auto py = static_cast<int>(std::floor(this->mCenterY + dx * this->mSin + dy * this->mCos));
				if (px < 0 || py < 0 || px >= this->mWidth || py >= this->mHeight) continue;

				const auto* p = this->mPixels + static_cast<ptrdiff_t>(py) * this->mStride
				              + static_cast<ptrdiff_t>(px) * 4;
				uint32_t b = p[0];
				uint32_t g = p[1];
				uint32_t r = p[2];
				auto bucket = ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
				this->mCounts[bucket]++;
				this->mSumR[bucket] += r;
				this->mSumG[bucket] += g;
				this->mSumB[bucket] += b;
				total++;
			}
		}

		if (total == 0) return {};

		size_t background = 0;
		for (size_t i = 1; i < BucketCount; i++) {
			if (this->mCounts[i] > this->mCounts[background]) background = i;
		}
		auto backgroundColor = this->average(background);

		auto textBucket = std::numeric_limits<size_t>::max();
		for (size_t i = 0; i < BucketCount; i++) {
			if (this->mCounts[i] == 0 || i == background) continue;
			if (textBucket != std::numeric_limits<size_t>::max()
			    && this->mCounts[i] <= this->mCounts[textBucket])
			{
				continue;
			}
			auto color = this->average(i);
			auto dr = color.red() - backgroundColor.red();
			auto dg = color.green() - backgroundColor.green();
			auto db = color.blue() - backgroundColor.blue();
			if (dr * dr + dg * dg + db * db >= MinTextDistance * MinTextDistance) textBucket = i;
		}

		QColor textColor;
		if (textBucket != std::numeric_limits<size_t>::max()) {
			textColor = this->average(textBucket);
		} else {
			auto luminance = 0.299 * backgroundColor.red() + 0.587 * backgroundColor.green()
			               + 0.114 * backgroundColor.blue();
			textColor = luminance > 140 ? QColor(0, 0, 0) : QColor(255, 255, 255);
		}

		return {backgroundColor.name(QColor::HexRgb), textColor.name(QColor::HexRgb)};
	}

private:
	static constexpr size_t BucketCount = 4096;
	static constexpr double MaxSamples = 16384;
	static constexpr int MinTextDistance = 80;

	[[nodiscard]] QColor average(size_t bucket) const {
		auto count = this->mCounts[bucket];
		return QColor(
		    static_cast<int>(this->mSumR[bucket] / count),
		    static_cast<int>(this->mSumG[bucket] / count),
		    static_cast<int>(this->mSumB[bucket] / count)
		);
	}

	const uint8_t* mPixels;
	int mWidth;
	int mHeight;
	int mStride;
	double mCenterX;
	double mCenterY;
	double mCos;
	double mSin;
	std::vector<uint32_t> mCounts;
	std::vector<uint32_t> mSumR;
	std::vector<uint32_t> mSumG;
	std::vector<uint32_t> mSumB;
};

QVariantList buildLines(const OcrResult& result, const SoftwareBitmap& bitmap) {
	QVariantList lines;

	auto angleRef = result.TextAngle();
	auto angle = angleRef ? angleRef.Value() : 0.0;
	auto imageWidth = bitmap.PixelWidth();
	auto imageHeight = bitmap.PixelHeight();

	BitmapBuffer buffer {nullptr};
	winrt::Windows::Foundation::IMemoryBufferReference reference {nullptr};
	std::unique_ptr<LineColorSampler> sampler;
	try {
		buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Read);
		reference = buffer.CreateReference();
		auto plane = buffer.GetPlaneDescription(0);
		sampler = std::make_unique<LineColorSampler>(
		    reference.data() + plane.StartIndex,
		    plane.Width,
		    plane.Height,
		    plane.Stride,
		    angle
		);
	} catch (const winrt::hresult_error& e) {
		qCWarning(logOcrWorker) << "Couldn't read the bitmap's pixels:" << hresultError(e);
	}

	for (const auto& line: result.Lines()) {
		QString text;
		QString previousWord;
		auto left = std::numeric_limits<double>::max();
		auto top = std::numeric_limits<double>::max();
		auto right = std::numeric_limits<double>::lowest();
		auto bottom = std::numeric_limits<double>::lowest();

		for (const auto& word: line.Words()) {
			auto wordText = QString::fromWCharArray(word.Text().c_str());
			if (wordText.isEmpty()) continue;
			if (!text.isEmpty()
			    && !(isCjk(lastCodePoint(previousWord)) && isCjk(firstCodePoint(wordText))))
			{
				text += QLatin1Char(' ');
			}
			text += wordText;
			previousWord = wordText;

			auto rect = word.BoundingRect();
			left = std::min(left, static_cast<double>(rect.X));
			top = std::min(top, static_cast<double>(rect.Y));
			right = std::max(right, static_cast<double>(rect.X + rect.Width));
			bottom = std::max(bottom, static_cast<double>(rect.Y + rect.Height));
		}

		if (text.isEmpty() || right <= left || bottom <= top) continue;

		auto rect = QRectF(left, top, right - left, bottom - top);
		QVariantMap entry;
		entry.insert(QStringLiteral("text"), text);
		entry.insert(QStringLiteral("x"), rect.x());
		entry.insert(QStringLiteral("y"), rect.y());
		entry.insert(QStringLiteral("width"), rect.width());
		entry.insert(QStringLiteral("height"), rect.height());
		entry.insert(QStringLiteral("angle"), angle);
		entry.insert(QStringLiteral("imageWidth"), imageWidth);
		entry.insert(QStringLiteral("imageHeight"), imageHeight);

		if (sampler) {
			auto [background, foreground] = sampler->colors(rect);
			entry.insert(QStringLiteral("backgroundColor"), background);
			entry.insert(QStringLiteral("textColor"), foreground);
		}

		lines.append(entry);
	}

	sampler.reset();
	if (reference) reference.Close();
	if (buffer) buffer.Close();

	return lines;
}
} // namespace

OcrWorker::OcrWorker(Ocr* frontend): mFrontend(frontend) {}

OcrWorker::~OcrWorker() = default;

void OcrWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->mApartmentInitialized = true;
	} catch (const winrt::hresult_error& e) {
		qCWarning(logOcrWorker) << "init_apartment(multi_threaded) failed:" << hresultError(e);
	}
}

void OcrWorker::shutdown() {
	this->mEngine = nullptr;

	if (this->mApartmentInitialized) {
		winrt::uninit_apartment();
		this->mApartmentInitialized = false;
	}
}

winrt::Windows::Media::Ocr::OcrEngine OcrWorker::engine() {
	if (this->mEngine) return this->mEngine;

	try {
		this->mEngine = OcrEngine::TryCreateFromUserProfileLanguages();
	} catch (const winrt::hresult_error& e) {
		qCDebug(logOcrWorker) << "TryCreateFromUserProfileLanguages failed:" << hresultError(e);
	}

	if (!this->mEngine) {
		try {
			auto available = OcrEngine::AvailableRecognizerLanguages();
			if (available.Size() > 0) {
				this->mEngine = OcrEngine::TryCreateFromLanguage(available.GetAt(0));
			}
		} catch (const winrt::hresult_error& e) {
			qCWarning(logOcrWorker) << "AvailableRecognizerLanguages failed:" << hresultError(e);
		}
	}

	return this->mEngine;
}

void OcrWorker::cmdRecognize(int requestId, const QString& path) {
	QString resultText;
	QVariantList lines;
	auto ok = false;
	QString error;

	try {
		auto ocrEngine = this->engine();
		if (!ocrEngine) {
			error = QStringLiteral(
			    "No OCR language installed (Settings > Time & language > Language & region > "
			    "add a language, then install its handwriting/OCR optional feature)"
			);
		} else {
			auto wpath = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toStdWString();
			auto file = StorageFile::GetFileFromPathAsync(winrt::hstring(wpath)).get();
			auto stream = file.OpenAsync(FileAccessMode::Read).get();
			auto decoder = BitmapDecoder::CreateAsync(stream).get();
			auto bitmap =
			    decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied)
			        .get();

			auto result = ocrEngine.RecognizeAsync(bitmap).get();
			resultText = QString::fromWCharArray(result.Text().c_str());
			lines = buildLines(result, bitmap);
			ok = true;
		}
	} catch (const winrt::hresult_error& e) {
		error = QStringLiteral("OCR failed (%1)").arg(hresultError(e));
		qCWarning(logOcrWorker) << "Recognition failed for" << path << ":" << error;
	}

	QMetaObject::invokeMethod(
	    QCoreApplication::instance(),
	    [frontend = this->mFrontend, requestId, resultText, ok, error, lines] {
		    if (frontend) frontend->backendDone(requestId, resultText, ok, error, lines);
	    },
	    Qt::QueuedConnection
	);
}

} // namespace qs::windows::sys
