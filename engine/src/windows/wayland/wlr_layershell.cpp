#include "wlr_layershell.hpp"

#include <qobject.h>
#include <qstring.h>

#include "../panel_window.hpp"

namespace qs::wayland::layershell {

using qs::windows::PanelKeyboardFocus;
using qs::windows::PanelLayer;
using qs::windows::WinPanelInterface;
using qs::windows::WinPanelWindow;

WlrLayershell::WlrLayershell(WinPanelWindow* panel): QObject(panel), panel(panel) {
	// clang-format off
	QObject::connect(panel, &WinPanelWindow::layerChanged, this, &WlrLayershell::layerChanged);
	QObject::connect(panel, &WinPanelWindow::namespaceChanged, this, &WlrLayershell::namespaceChanged);
	QObject::connect(panel, &WinPanelWindow::keyboardFocusChanged, this, &WlrLayershell::keyboardFocusChanged);
	QObject::connect(panel, &WinPanelWindow::marginsChanged, this, &WlrLayershell::marginsChanged);
	QObject::connect(panel, &WinPanelWindow::exclusiveZoneChanged, this, &WlrLayershell::exclusiveZoneChanged);
	QObject::connect(panel, &WinPanelWindow::exclusionModeChanged, this, &WlrLayershell::exclusionModeChanged);
	// clang-format on
}

qint32 WlrLayershell::exclusiveZone() const { return this->panel->exclusiveZone(); }
void WlrLayershell::setExclusiveZone(qint32 zone) { this->panel->setExclusiveZone(zone); }

ExclusionMode::Enum WlrLayershell::exclusionMode() const { return this->panel->exclusionMode(); }
void WlrLayershell::setExclusionMode(ExclusionMode::Enum mode) { this->panel->setExclusionMode(mode); }

Margins WlrLayershell::margins() const { return this->panel->margins(); }
void WlrLayershell::setMargins(Margins margins) { this->panel->setMargins(margins); }

WlrLayer::Enum WlrLayershell::layer() const {
	return static_cast<WlrLayer::Enum>(this->panel->layer());
}

void WlrLayershell::setLayer(WlrLayer::Enum layer) {
	this->panel->setLayer(static_cast<PanelLayer>(layer));
}

QString WlrLayershell::ns() const { return this->panel->ns(); }
void WlrLayershell::setNamespace(const QString& ns) { this->panel->setNamespace(ns); }

WlrKeyboardFocus::Enum WlrLayershell::keyboardFocus() const {
	return static_cast<WlrKeyboardFocus::Enum>(this->panel->keyboardFocus());
}

void WlrLayershell::setKeyboardFocus(WlrKeyboardFocus::Enum focus) {
	this->panel->setKeyboardFocus(static_cast<PanelKeyboardFocus>(focus));
}

WlrLayershell* WlrLayershell::qmlAttachedProperties(QObject* object) {
	WinPanelWindow* panel = nullptr;

	if (auto* iface = qobject_cast<WinPanelInterface*>(object)) panel = iface->panel;
	else if (auto* window = qobject_cast<WinPanelWindow*>(object)) panel = window;
	else return nullptr;

	if (auto* attached = qobject_cast<WlrLayershell*>(panel->layershellAttached())) {
		return attached;
	}

	auto* attached = new WlrLayershell(panel);
	panel->setLayershellAttached(attached);
	return attached;
}

} // namespace qs::wayland::layershell
