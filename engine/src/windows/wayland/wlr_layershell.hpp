#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtclasshelpermacros.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../panel_window.hpp"

namespace qs::wayland::layershell {

namespace WlrLayer { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Background = 0,
	Bottom = 1,
	Top = 2,
	Overlay = 3,
};
Q_ENUM_NS(Enum);

} // namespace WlrLayer

namespace WlrKeyboardFocus { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	None = 0,
	Exclusive = 1,
	OnDemand = 2,
};
Q_ENUM_NS(Enum);

} // namespace WlrKeyboardFocus

class WlrLayershell: public QObject {
	// clang-format off
	Q_OBJECT;
	Q_PROPERTY(qs::wayland::layershell::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY layerChanged);
	Q_PROPERTY(QString namespace READ ns WRITE setNamespace NOTIFY namespaceChanged);
	Q_PROPERTY(qs::wayland::layershell::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE setKeyboardFocus NOTIFY keyboardFocusChanged);
	/// Same as the window's margins (some Linux configs set them through WlrLayershell.margins).
	Q_PROPERTY(Margins margins READ margins WRITE setMargins NOTIFY marginsChanged);
	Q_PROPERTY(qint32 exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged);
	Q_PROPERTY(ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode NOTIFY exclusionModeChanged);
	QML_ATTACHED(WlrLayershell);
	QML_NAMED_ELEMENT(WlrLayershell);
	QML_UNCREATABLE("WlrLayershell is only available as an attached object on Windows.");
	// clang-format on

public:
	explicit WlrLayershell(qs::windows::WinPanelWindow* panel);
	Q_DISABLE_COPY_MOVE(WlrLayershell);

	[[nodiscard]] WlrLayer::Enum layer() const;
	void setLayer(WlrLayer::Enum layer);

	[[nodiscard]] QString ns() const;
	void setNamespace(const QString& ns);

	[[nodiscard]] WlrKeyboardFocus::Enum keyboardFocus() const;
	void setKeyboardFocus(WlrKeyboardFocus::Enum focus);

	[[nodiscard]] Margins margins() const;
	void setMargins(Margins margins);

	[[nodiscard]] qint32 exclusiveZone() const;
	void setExclusiveZone(qint32 zone);

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const;
	void setExclusionMode(ExclusionMode::Enum mode);

	static WlrLayershell* qmlAttachedProperties(QObject* object);

signals:
	void layerChanged();
	void namespaceChanged();
	void keyboardFocusChanged();
	void marginsChanged();
	void exclusiveZoneChanged();
	void exclusionModeChanged();

private:
	qs::windows::WinPanelWindow* panel;
};

} // namespace qs::wayland::layershell
