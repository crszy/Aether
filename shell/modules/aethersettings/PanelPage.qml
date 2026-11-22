// This panel: DarkShadowShell's own look settings (motion, shapes, text, components, sounds) plus which settings
// window Aether opens.
import QtQuick
import qs.services as Cae

PageScroll {
    id: page

    SectionTitle {
        text: "Which settings opens"
        sub: "What the settings key, the bar and the launcher open."
    }
    SegRow {
        label: "Settings window"
        options: [{ value: "aether", label: "Aether" }, { value: "rice", label: "Aether classic" }, { value: "caelestia", label: "Caelestia" }, { value: "noctalia", label: "Noctalia" }]
        current: Cae.WinSettings.settingsStyle
        onChosen: v => Cae.WinSettings.settingsStyle = v
    }
    ToggleRow {
        label: "Always on top"
        desc: "Keep settings above other windows (tilers leave it alone)"
        checked: Cae.WinSettings.nexusTopmost
        onToggled: v => Cae.WinSettings.nexusTopmost = v
    }

    SectionTitle {
        text: "Motion"
    }
    SegRow {
        label: "Style"
        desc: "Expressive: a little spring. Standard: calm curves"
        options: [{ value: "expressive", label: "Expressive" }, { value: "standard", label: "Standard" }]
        current: Prefs.motion
        onChosen: v => Prefs.setMotion(v)
    }
    SliderRow {
        label: "Speed"
        from: 0.5
        to: 2
        step: 0.05
        decimals: 2
        unit: "×"
        value: Prefs.motionSpeed
        onCommitted: v => Prefs.setMotionSpeed(v)
    }
    SliderRow {
        label: "Scroll speed"
        desc: "How far one wheel notch scrolls a page"
        from: 0.3
        to: 3
        step: 0.05
        decimals: 2
        unit: "×"
        value: Prefs.scroll
        onCommitted: v => Prefs.setScroll(v)
    }
    ToggleRow {
        label: "Shooting stars"
        desc: "The stars drifting behind the pages"
        checked: Prefs.stars
        onToggled: v => Prefs.setStars(v)
    }

    SectionTitle {
        text: "Shapes and text"
    }
    SliderRow {
        label: "Corners"
        desc: "Rounder or squarer cards and buttons"
        from: 0.3
        to: 1.5
        step: 0.05
        decimals: 2
        unit: "×"
        value: Prefs.cornerScale
        onCommitted: v => Prefs.setCornerScale(v)
    }
    SliderRow {
        label: "Text size"
        from: 0.85
        to: 1.3
        step: 0.05
        decimals: 2
        unit: "×"
        value: Prefs.textScale
        onCommitted: v => Prefs.setTextScale(v)
    }
    ToggleRow {
        label: "Heavier headings"
        checked: Prefs.emphHeadings
        onToggled: v => Prefs.setEmphHeadings(v)
    }
    ToggleRow {
        label: "Round letters"
        desc: "Google Sans Flex only (its roundness axis)"
        checked: Prefs.roundText
        onToggled: v => Prefs.setRoundText(v)
    }
    SliderRow {
        label: "Window size"
        desc: "Used the next time settings opens"
        from: 0.75
        to: 1.5
        step: 0.05
        decimals: 2
        unit: "×"
        value: Prefs.size
        onCommitted: v => Prefs.setSize(v)
    }

    SectionTitle {
        text: "Components"
    }
    ToggleRow {
        label: "Icons in switches"
        desc: "A check or a cross in the switch handle"
        checked: Prefs.switchIcons
        onToggled: v => Prefs.setSwitchIcons(v)
    }
    SliderRow {
        label: "Slider thickness"
        from: 0.6
        to: 1.6
        step: 0.1
        decimals: 1
        unit: "×"
        value: Prefs.sliderThick
        onCommitted: v => Prefs.setSliderThick(v)
    }
    ToggleRow {
        label: "Wavy sliders"
        desc: "The track waves while you drag"
        checked: Prefs.wavy
        onToggled: v => Prefs.setWavy(v)
    }

    SectionTitle {
        text: "Sounds"
        sub: "Soft taps when you click, switch and slide in this window (DarkShadowShell's tap pack). Nothing else plays them."
    }
    ToggleRow {
        label: "Interface sounds"
        checked: Prefs.sfx
        onToggled: v => Prefs.setSfx(v)
    }
    SliderRow {
        label: "Volume"
        from: 0
        to: 100
        step: 1
        unit: "%"
        value: Prefs.sfxVolume
        onCommitted: v => Prefs.setSfxVolume(v)
    }
}
