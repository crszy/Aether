#pragma once

#include <memory>

#include <qt_windows.h>

#include <d3d11.h>

#include <qrect.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>

namespace qs::windows::recorder {

struct RegionCaptureState;

class RegionCapture {
public:
	RegionCapture() = default;
	~RegionCapture();
	Q_DISABLE_COPY_MOVE(RegionCapture);

	bool start(ID3D11Device* device, const QRect& region, bool cursor, QString* error);
	void stop();

	[[nodiscard]] ID3D11Texture2D* composite() const;
	[[nodiscard]] bool primed() const;
	[[nodiscard]] bool lost() const;

private:
	std::shared_ptr<RegionCaptureState> state;
};

} // namespace qs::windows::recorder
