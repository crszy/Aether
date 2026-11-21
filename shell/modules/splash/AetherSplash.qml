pragma ComponentBehavior: Bound

// The Aether splash (WinSettings.splashStyle "aether"): a port of the user's AetherSplash (splash.cpp, GDI+) to a
// QML Canvas, drawn the same way - the tonal pill springing open into the card, two slow morphing blobs, the Aether
// "A" with its orbit ring (back and front halves) and the quarter-turning star, the M3 Expressive wavy progress line,
// status steps and rotating tips, then the ring closes, the logo slides left, "AETHER" types in and it all fades.
// 1 launch in 20 is the lucky one (clover green, a clover instead of the star, a "Lucky launch" chip).
// Colours and motion: AetherStyle. Emits finished() when the fade-out is done.
import QtQuick
import qs.services
import "../aether/AetherDraw.js" as Draw

Item {
    id: root

    signal finished

    property real loadSeconds: 2.4       // how long the progress line takes (WinSettings.splashMs)
    property bool running: visible

    readonly property int cw: 640
    readonly property int ch: 400
    readonly property int m: 48

    implicitWidth: cw + 2 * m
    implicitHeight: ch + 2 * m

    // ---- timeline state (seconds) ----
    property real tRaw: 0
    property real clockT: 0
    property real pDisp: 0
    property real doneAt: -1
    property bool emitted: false
    property double startMs: 0
    property real prevT: 0
    property var tips: []

    readonly property real t0: 0.5

    function restart(): void {
        AetherStyle.lucky = Math.random() < 0.05;
        const all = ["Every panel, bar and widget is yours to rearrange.", "Your shell, your rules. Aether adapts to how you work.",
            "Blur, transparency and motion are built in, not bolted on.", "Small, fast and fluid. Aether stays out of your way.",
            "Everything you see here is customizable.", "Rice it once, enjoy it every day.",
            "Dotfiles are great. A shell that just works is better.", "A desktop should feel like yours, not like it's rented.",
            "Tiling, floating, scrolling. Use whatever fits the moment.", "Good animations are felt, not noticed.",
            "Keyboard first. Your mouse is optional.", "Less bloat, more desktop.", "Windows underneath, Linux attitude on top.",
            "Take a breath. Your desktop is almost ready.", "Smooth is a feature.", "Fewer clicks. More flow.", "Work your way, at your own pace."];
        for (let i = all.length - 1; i > 0; i--) {
            const j = Math.floor(Math.random() * (i + 1));
            [all[i], all[j]] = [all[j], all[i]];
        }
        const first = AetherStyle.lucky ? ["You got the lucky splash. It only shows up 1 in 20 launches."] : [];
        root.tips = first.concat(["Linux gives you choices. Aether lets you combine them."], all);
        root.tRaw = 0;
        root.clockT = 0;
        root.pDisp = 0;
        root.doneAt = -1;
        root.prevT = 0;
        root.emitted = false;
        root.startMs = Date.now();
        canvas.requestPaint();
    }

    Component.onCompleted: restart()
    onVisibleChanged: if (visible) restart()

    Timer {
        interval: 16
        repeat: true
        running: root.running && !root.emitted
        onTriggered: {
            root.tRaw = (Date.now() - root.startMs) / 1000;
            canvas.requestPaint();
        }
    }

    Canvas {
        id: canvas

        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: root.draw(getContext("2d"))
    }

    // ================= drawing (splash.cpp Render) =================
    function draw(g: var): void {
        const S = AetherStyle;
        const W = root.cw, H = root.ch, M = root.m;
        const traw = root.tRaw;
        const dt = Math.min(0.1, Math.max(0, traw - root.prevT));
        root.prevT = traw;

        g.reset();
        g.clearRect(0, 0, root.width, root.height);
        const k = Math.min(root.width / (W + 2 * M), root.height / (H + 2 * M));
        g.scale(k, k);

        const tc = Math.max(0, traw - root.t0);
        const LOAD = Math.max(1.2, root.loadSeconds);
        if (root.doneAt < 0 && tc > 0.5) {
            root.clockT += dt;
            root.pDisp = S.bez(0.3, 0.1, 0.2, 1, root.clockT / LOAD);
            if (root.clockT >= LOAD)
                root.doneAt = tc;
        }
        const tt = root.doneAt < 0 ? -1 : tc - root.doneAt;
        const p = root.doneAt >= 0 ? 1 : root.pDisp;
        const te = root.doneAt < 0 ? -1 : traw - (root.t0 + root.doneAt + 2.6);
        const arcCol = S.primary;
        const cxw = W / 2, cyh = H / 2;

        let xs = 1, fade = 1;
        if (te > 0) {
            const e = S.emphAcc(te / 0.28);
            xs = 1 - 0.05 * e;
            fade = 1 - e;
        }
        g.globalAlpha = S.clamp01(fade);
        g.translate(M + cxw, M + cyh);
        g.scale(xs, xs);
        g.translate(-cxw, -cyh);

        // container transform: a small tonal pill springs open into the surface
        const lt = traw - 0.15;
        const w = Draw.lerp(64, W, S.spatial(lt - 0.04)), h = Draw.lerp(64, H, S.spatial(lt));
        const r = Draw.lerp(Math.min(w, h) / 2, S.cornerXL, S.emphDec(lt / 0.5));
        const ca = S.effects(traw - 0.3);
        const cardCol = S.mix(S.primaryContainer, S.surface, ca);
        const sh = S.clamp01((w - 64) / 300);
        for (let i = 0; i < 8; i++) {
            const gr = i * 4;
            Draw.rr(g, cxw - w / 2 - gr, cyh - h / 2 - gr + 8, w + 2 * gr, h + 2 * gr, r + gr);
            g.fillStyle = `rgba(0,0,0,${5 * sh / 255})`;
            g.fill();
        }
        Draw.rr(g, cxw - w / 2, cyh - h / 2, w, h, r);
        g.fillStyle = S.css(cardCol, 1);
        g.fill();

        g.save();
        Draw.rr(g, cxw - w / 2, cyh - h / 2, w, h, r);
        g.clip();
        const cs = Draw.lerp(0.94, 1, S.emphDec((traw - 0.3) / 0.6));
        g.translate(cxw, cyh);
        g.scale(cs, cs);
        g.translate(-cxw, -cyh);

        // background: two slow morphing tonal shapes
        Draw.blob(g, 40, 30, 260, tc, 0, S.css(S.primaryContainer, 46 / 255));
        Draw.blob(g, W - 30, H + 10, 230, tc, 2, S.css(S.tertiary, 13 / 255));

        const ui = tt < 0 ? 1 : 1 - S.clamp01(tt / 0.2);
        const close = tt < 0 ? 0 : S.emphDec(tt / 0.6);
        const mo = S.spatial(tt - 0.25);
        const lx = Draw.lerp(W / 2, 174, mo), ly = Draw.lerp(150, 190, mo), ls = Draw.lerp(0.193, 0.15, mo);
        Draw.logo(S, g, tc, close, lx, ly, ls, arcCol);

        const font = "'Segoe UI Variable Text', 'Segoe UI', sans-serif";
        if (ui > 0.01 && tc > 0.5) {
            const a = S.effects(tc - 0.5) * ui;
            const steps = ["Starting up", "Loading modules", "Preparing your shell", "Almost ready"];
            g.font = `14px ${font}`;
            g.textAlign = "center";
            g.textBaseline = "middle";
            g.fillStyle = S.css(S.textSub, a);
            g.fillText(steps[Math.min(3, Math.floor(p * 4))], W / 2, 269);
            const flat = S.clamp01((p - 0.96) / 0.04);
            Draw.wavyBar(S, g, W / 2 - 130, W / 2 + 130, 296, p, tc, flat, arcCol, a);
            const tp = tc - 0.9;
            if (tp > 0 && root.tips.length) {
                const ti = Math.floor(tp / 5) % root.tips.length, u = tp % 5;
                const ta = S.clamp01(u / 0.35) * (1 - S.clamp01((u - 4.65) / 0.35)) * a;
                const dy = (1 - S.emphDec(u / 0.5)) * 10 - S.clamp01((u - 4.65) / 0.35) * 6;
                g.fillStyle = S.css(S.textSub, 200 / 255 * ta);
                g.fillText(root.tips[ti], W / 2, H - 44 + dy);
            }
        }

        if (tt > 0.45) {   // wordmark
            const dh = 84 * S.emphDec((tt - 0.45) / 0.5);
            g.strokeStyle = S.css(S.outlineVar, 1);
            g.lineWidth = 1.5;
            g.beginPath();
            g.moveTo(281, ly - dh / 2);
            g.lineTo(281, ly + dh / 2);
            g.stroke();
            g.font = `600 40px 'Segoe UI Semibold', 'Segoe UI', sans-serif`;
            g.textAlign = "center";
            g.textBaseline = "middle";
            const word = "AETHER";
            for (let i = 0; i < 6; i++) {
                const u = S.emphDec((tt - 0.55 - 0.045 * i) / 0.55);
                if (u <= 0)
                    continue;
                g.fillStyle = S.css(S.textMain, u);
                g.fillText(word[i], 311 + 38 * i + 19, ly + (1 - u) * 14);
            }
        }
        if (AetherStyle.lucky && tt > 1.3) {
            const u = S.emphDec((tt - 1.3) / 0.5);
            Draw.rr(g, 311, ly + 36 + (1 - u) * 8, 132, 26, 13);
            g.fillStyle = S.css(S.primaryContainer, u);
            g.fill();
            Draw.clover(g, 326, ly + 49 + (1 - u) * 8, 6, 0, S.css(S.textOnPrimaryContainer, u));
            g.font = `13px ${font}`;
            g.textAlign = "left";
            g.fillStyle = S.css(S.textOnPrimaryContainer, u);
            g.fillText("Lucky launch", 340, ly + 49 + (1 - u) * 8);
        }
        if (ca < 0.999) {   // content wash while the pill is still opening
            g.fillStyle = S.css(cardCol, 1 - ca);
            g.fillRect(-60, -60, W + 120, H + 120);
        }
        g.restore();

        if (ca < 0.99)
            Draw.star4(g, cxw, cyh, 15 * (1 - ca), traw * 1.2, S.css(S.textOnPrimaryContainer, 1 - ca));
        Draw.rr(g, cxw - w / 2, cyh - h / 2, w, h, r);
        g.strokeStyle = S.css(S.outlineVar, 150 / 255 * ca);
        g.lineWidth = 1;
        g.stroke();

        if (te > 0.3 && !root.emitted) {
            root.emitted = true;
            root.finished();
        }
    }
}
