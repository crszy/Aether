#include "night_light.hpp"

#include "gamma.hpp"

namespace qs::windows::sys {

void NightLight::enable(int colorTemperatureKelvin) {
	GammaController::instance()->setTemperature(colorTemperatureKelvin);
}

void NightLight::disable() { GammaController::instance()->setTemperature(0); }

void NightLight::setDim(const QString& screenName, qreal factor) {
	GammaController::instance()->setDim(screenName, factor);
}

qreal NightLight::probeDimFloor(const QString& screenName) {
	return GammaController::instance()->probeDimFloor(screenName);
}

} // namespace qs::windows::sys
