#include "gamma.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qpointer.h>
#include <qscreen.h>
#include <qstring.h>

#include "../../core/logcat.hpp"

namespace qs::windows::sys {

namespace {
QS_LOGGING_CATEGORY(logGamma, "quickshell.windows.gamma", QtInfoMsg);

constexpr qreal BRIGHTNESS_STEP = 0.05;

void factorsForTemperature(int kelvin, double factors[3]) { // NOLINT
	auto temp = std::clamp(kelvin, 1000, 40000) / 100.0;

	double red = 255.0;
	double green = 255.0;
	double blue = 255.0;

	if (temp > 66.0) red = 329.698727446 * std::pow(temp - 60.0, -0.1332047592);

	if (temp <= 66.0) green = 99.4708025861 * std::log(temp) - 161.1195681661;
	else green = 288.1221695283 * std::pow(temp - 60.0, -0.0755148492);

	if (temp >= 66.0) blue = 255.0;
	else if (temp <= 19.0) blue = 0.0;
	else blue = 138.5177312231 * std::log(temp - 10.0) - 305.0447927307;

	factors[0] = std::clamp(red, 0.0, 255.0) / 255.0;
	factors[1] = std::clamp(green, 0.0, 255.0) / 255.0;
	factors[2] = std::clamp(blue, 0.0, 255.0) / 255.0;
}

HDC screenDc(const QString& screenName) {
	auto name = screenName.toStdWString();
	return CreateDCW(L"DISPLAY", name.c_str(), nullptr, nullptr);
}

} // namespace

GammaController* GammaController::instance() {
	static QPointer<GammaController> controller; // NOLINT

	if (controller.isNull()) {
		controller = new GammaController(QCoreApplication::instance());
	}

	return controller.data();
}

GammaController::GammaController(QObject* parent): QObject(parent) {
	QObject::connect(
	    QCoreApplication::instance(),
	    &QCoreApplication::aboutToQuit,
	    this,
	    &GammaController::restoreAll
	);
}

void GammaController::setTemperature(int kelvin) {
	this->mKelvin = kelvin;

	for (auto* screen: QGuiApplication::screens()) {
		this->reapply(screen->name());
	}
}

void GammaController::setBrightness(const QString& screenName, qreal brightness) {
	auto clamped = std::clamp(brightness, 0.0, 1.0);
	if (clamped >= 1.0) this->backlightFactor.remove(screenName);
	else this->backlightFactor.insert(screenName, clamped);
	this->reapply(screenName);
}

void GammaController::setDim(const QString& screenName, qreal factor) {
	auto clamped = std::clamp(factor, 0.0, 1.0);
	if (clamped >= 1.0) this->dimFactor.remove(screenName);
	else this->dimFactor.insert(screenName, clamped);
	this->reapply(screenName);
}

qreal GammaController::probeDimFloor(const QString& screenName) {
	auto floor = this->floorFor(screenName);
	this->reapply(screenName);
	return floor;
}

bool GammaController::dims(const QString& screenName) const {
	return this->backlightFactor.contains(screenName);
}

qreal GammaController::floorFor(const QString& screenName) {
	auto iter = this->floors.constFind(screenName);
	if (iter != this->floors.constEnd()) return *iter;

	auto kelvin = std::exchange(this->mKelvin, 0);
	auto floor = 0.0;
	while (floor < 1.0 && !this->apply(screenName, floor)) floor += BRIGHTNESS_STEP;
	this->mKelvin = kelvin;

	floor = std::min(floor, 1.0);
	qCInfo(logGamma) << "Software brightness of" << screenName << "goes down to" << floor;
	this->floors.insert(screenName, floor);
	return floor;
}

void GammaController::reapply(const QString& screenName) {
	auto backlight = this->backlightFactor.value(screenName, 1.0);
	auto dim = this->dimFactor.value(screenName, 1.0);
	auto combined = std::clamp(backlight * dim, 0.0, 1.0);

	auto floor = combined < 1.0 ? this->floorFor(screenName) : 1.0;
	auto wanted = floor + combined * (1.0 - floor);
	auto applied = wanted;

	while (!this->apply(screenName, applied) && applied < 1.0) {
		applied = std::min(1.0, applied + BRIGHTNESS_STEP);
	}

	if (applied > wanted) {
		qCDebug(logGamma) << "Windows refused dimming" << screenName << "to" << wanted << "; went to"
		                  << applied;
	}
}

bool GammaController::apply(const QString& screenName, qreal brightness) {
	auto* hdc = screenDc(screenName);
	if (hdc == nullptr) return false;

	auto neutral = this->mKelvin <= 0 && brightness >= 1.0;
	auto ok = false;

	if (neutral) {
		auto iter = this->originals.constFind(screenName);
		ok = iter == this->originals.constEnd()
		  || SetDeviceGammaRamp(hdc, const_cast<WORD*>(&iter.value().values[0][0])); // NOLINT
	} else {
		if (!this->originals.contains(screenName)) {
			Ramp original {};
			if (GetDeviceGammaRamp(hdc, original.values)) this->originals.insert(screenName, original);
		}

		double factors[3] = {1.0, 1.0, 1.0}; // NOLINT
		if (this->mKelvin > 0) factorsForTemperature(this->mKelvin, factors);

		Ramp ramp {};
		for (auto channel = 0; channel < 3; channel++) {
			for (auto i = 0; i < 256; i++) {
				auto value = static_cast<double>(i) * 257.0 * factors[channel] * brightness;
				ramp.values[channel][i] = static_cast<WORD>(std::clamp(value, 0.0, 65535.0));
			}
		}

		ok = SetDeviceGammaRamp(hdc, ramp.values) != FALSE;
		if (!ok) qCDebug(logGamma) << "SetDeviceGammaRamp refused for" << screenName << brightness;
	}

	DeleteDC(hdc);
	return ok;
}

void GammaController::restoreAll() {
	for (auto iter = this->originals.constBegin(); iter != this->originals.constEnd(); ++iter) {
		auto* hdc = screenDc(iter.key());
		if (hdc == nullptr) continue;

		SetDeviceGammaRamp(hdc, const_cast<WORD*>(&iter.value().values[0][0])); // NOLINT
		DeleteDC(hdc);
	}

	this->mKelvin = 0;
	this->backlightFactor.clear();
	this->dimFactor.clear();
}

} // namespace qs::windows::sys
