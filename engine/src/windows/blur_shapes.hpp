#pragma once

#include <optional>

#include <qlist.h>
#include <qrect.h>
#include <qregion.h>
#include <qtypes.h>

class QQuickItem;
class QQuickWindow;

namespace qs::windows {

struct BlurShape {
	QRectF rect;
	qreal radius = 0;
	QRectF clip;

	[[nodiscard]] bool fuzzyEquals(const BlurShape& other) const;
};

struct BlurShapeQuery {
	std::optional<qreal> ignoreAlpha;
	QRegion mask;
	bool hasMask = false;
	qreal dpr = 1;
};

struct BlurShapeResult {
	QList<BlurShape> shapes;
	bool truncated = false;
};

[[nodiscard]] BlurShapeResult collectBlurShapes(QQuickWindow* window, const BlurShapeQuery& query);
[[nodiscard]] bool itemTreeDirty(QQuickWindow* window);
[[nodiscard]] bool framePending(QQuickWindow* window);

} // namespace qs::windows
