import QtQuick
import qs.end4.services
import qs.end4.modules.common
import qs.end4.modules.common.functions
import qs.end4.modules.common.widgets

QuickToggleModel {
    name: HyprlandAntiFlashbangShader.enabled ? (HyprlandAntiFlashbangShader.weak ? Translation.tr("Anti-flash: Weak") : Translation.tr("Anti-flash: Strong")) : Translation.tr("Anti-flashbang")
    tooltipText: `${Translation.tr("Anti-flashbang")}: ${HyprlandAntiFlashbangShader.enabled ? (HyprlandAntiFlashbangShader.weak ? Translation.tr("Weak") : Translation.tr("Strong")) : Translation.tr("Off")}`
    icon: HyprlandAntiFlashbangShader.enabled ? (!HyprlandAntiFlashbangShader.weak ? "flash_off" : "sunny_snowing") : "flash_on"
    toggled: HyprlandAntiFlashbangShader.enabled

    mainAction: () => {
        HyprlandAntiFlashbangShader.cycle()
    }
    hasMenu: true
}
