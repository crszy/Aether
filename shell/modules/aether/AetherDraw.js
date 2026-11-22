.pragma library
// The AetherSplash's drawing (splash.cpp), shared by everything in Aether style: the splash, AetherWavy, AetherBlobs,
// AetherLogo. Every function takes S = the AetherStyle singleton (colours + curves) and a Canvas 2D context g.

function lerp(a, b, t) {
    return a + (b - a) * t;
}

// rounded rectangle path
function rr(g, x, y, w, h, r) {
    r = Math.min(r, Math.min(w, h) / 2);
    g.beginPath();
    g.moveTo(x + r, y);
    g.arcTo(x + w, y, x + w, y + h, r);
    g.arcTo(x + w, y + h, x, y + h, r);
    g.arcTo(x, y + h, x, y, r);
    g.arcTo(x, y, x + w, y, r);
    g.closePath();
}

// the 4-point star (astroid)
function star4(g, cx, cy, r, rot, col) {
    g.beginPath();
    for (var i = 0; i < 40; i++) {
        var u = 2 * Math.PI * i / 40, co = Math.cos(u), si = Math.sin(u);
        var x = r * co * co * co, y = r * si * si * si;
        var px = cx + x * Math.cos(rot) - y * Math.sin(rot), py = cy + x * Math.sin(rot) + y * Math.cos(rot);
        if (i === 0)
            g.moveTo(px, py);
        else
            g.lineTo(px, py);
    }
    g.closePath();
    g.fillStyle = col;
    g.fill();
}

// soft lobed blob; lobe amplitudes drift so it morphs between "cookie" shapes
function blob(g, cx, cy, R, t, ph, col) {
    var a3 = 0.11 * (0.5 + 0.5 * Math.sin(t * 0.5 + ph)), a5 = 0.09 * (0.5 + 0.5 * Math.cos(t * 0.4 + ph)), rot = t * 0.08 + ph;
    g.beginPath();
    for (var i = 0; i < 120; i++) {
        var th = 2 * Math.PI * i / 120;
        var r = R * (1 + a3 * Math.cos(3 * (th - rot)) + a5 * Math.cos(5 * (th + rot * 0.6) + 1));
        if (i === 0)
            g.moveTo(cx + Math.cos(th) * r, cy + Math.sin(th) * r);
        else
            g.lineTo(cx + Math.cos(th) * r, cy + Math.sin(th) * r);
    }
    g.closePath();
    g.fillStyle = col;
    g.fill();
}

function clover(g, cx, cy, r, rot, col) {
    g.fillStyle = col;
    for (var k = 0; k < 4; k++) {
        var a = rot + k * Math.PI / 2;
        g.beginPath();
        g.ellipse(cx + Math.cos(a) * r * 0.5 - r * 0.56, cy + Math.sin(a) * r * 0.5 - r * 0.56, r * 1.12, r * 1.12);
        g.fill();
    }
}

// ---- the logo (logo space ~1254 px, centre ~628,590) ----
function ringPt(th) {
    var lx = 520 * Math.cos(th), ly = 150 * Math.sin(th), c = 0.9135, s = -0.4067;
    return [628 + lx * c - ly * s, 598 + lx * s + ly * c];
}

// the front or back half of an arc of the ring
function arcPath(g, a0, a1, front) {
    if (a1 <= a0)
        return;
    for (var m = Math.floor(a0 / Math.PI); m <= Math.floor(a1 / Math.PI); m++) {
        if ((((m % 2) + 2) % 2 === 0) !== front)
            continue;
        var s = Math.max(a0, m * Math.PI), e = Math.min(a1, (m + 1) * Math.PI);
        if (e <= s)
            continue;
        var n = Math.floor((e - s) / 0.05) + 2;
        g.beginPath();
        for (var i = 0; i < n; i++) {
            var pt = ringPt(s + (e - s) * i / (n - 1));
            if (i === 0)
                g.moveTo(pt[0], pt[1]);
            else
                g.lineTo(pt[0], pt[1]);
        }
        g.stroke();
    }
}

function ring(S, g, tc, eR, close, front, arcCol) {
    if (eR < 0.01)
        return;
    g.lineCap = "round";
    g.lineJoin = "round";
    g.strokeStyle = S.css(front ? S.outlineVar : S.mix(S.surface, S.outlineVar, 0.5), eR);
    g.lineWidth = 6;
    arcPath(g, 0, 2 * Math.PI, front);
    var head = tc * 2;
    var len = 0.9 + 1.2 * (0.5 + 0.5 * Math.sin(tc * 1.6));
    len += (2 * Math.PI + 0.02 - len) * close;
    g.strokeStyle = S.css(front ? arcCol : S.mix(S.surface, arcCol, 0.55), eR);
    g.lineWidth = 15;
    arcPath(g, head - len, head, front);
}

// the whole mark at (cx, cy), scale s (0.193 = the splash size). close 0..1 closes the arc into a full ring.
function logo(S, g, tc, close, cx, cy, s, arcCol) {
    g.save();
    g.translate(cx, cy);
    g.scale(s, s);
    g.translate(-628, -590);
    var eA = S.emphDec(tc / 0.8), eT = S.emphDec((tc - 0.12) / 0.8), eR = S.emphDec((tc - 0.25) / 0.9);
    var rs = 0.85 + 0.15 * eR;

    g.save();
    g.translate(628, 598);
    g.scale(rs, rs);
    g.translate(-628, -598);
    ring(S, g, tc, eR, close, false, arcCol);
    g.restore();

    g.save();
    g.translate(-50 * (1 - eA), -36 * (1 - eA) + Math.sin(tc * 1.2) * 2.5 * eA);
    g.beginPath();
    var up = [[630, 290], [842, 577], [748, 622], [630, 450], [427, 752], [285, 793]];
    g.moveTo(up[0][0], up[0][1]);
    for (var i = 1; i < up.length; i++)
        g.lineTo(up[i][0], up[i][1]);
    g.closePath();
    g.fillStyle = S.css(S.primary, eA);
    g.fill();
    g.restore();

    g.save();
    g.translate(50 * (1 - eT) + Math.cos(tc * 0.9) * 4 * eT, 36 * (1 - eT) + Math.sin(tc * 1.1) * 4 * eT);
    g.beginPath();
    g.moveTo(878, 628);
    g.lineTo(1035, 867);
    g.bezierCurveTo(900, 800, 750, 765, 628, 786);
    g.bezierCurveTo(740, 778, 800, 775, 848, 770);
    g.lineTo(790, 677);
    g.closePath();
    g.fillStyle = S.css(S.tertiary, eT);
    g.fill();
    g.restore();

    var step = tc / 1.6;
    var rot = (Math.floor(step) + S.emphDec(step - Math.floor(step))) * Math.PI / 2;
    var ss = S.expressive(tc - 0.6) * (1 + 0.05 * Math.sin(tc * 2));
    if (S.lucky)
        clover(g, 630, 603, 70 * ss, rot, S.css(S.textMain, 1));
    else
        star4(g, 630, 603, 72 * ss, rot, S.css(S.textMain, 1));

    g.save();
    g.translate(628, 598);
    g.scale(rs, rs);
    g.translate(-628, -598);
    ring(S, g, tc, eR, close, true, arcCol);
    g.restore();
    g.restore();
}

// ---- M3 Expressive wavy linear progress (x0..x1 at y, value p 0..1, time t, flat 0..1 = straightened) ----
function wavyBar(S, g, x0, x1, y, p, t, flat, col, a, thickness) {
    var lw = thickness || 4.5;
    var fw = (x1 - x0) * p, amp = 3 * (1 - flat) * (lw / 4.5), lam = 34;
    g.lineCap = "round";
    g.lineJoin = "round";
    g.lineWidth = lw;
    if (fw > 3 && amp < 0.05) {   // straightened: one stroke (hundreds of collinear 2 px segments leave seams)
        g.strokeStyle = S.css(col, a);
        g.beginPath();
        g.moveTo(x0, y);
        g.lineTo(x0 + fw, y);
        g.stroke();
    } else if (fw > 3) {
        g.strokeStyle = S.css(col, a);
        g.beginPath();
        for (var x = 0; x <= fw; x += 2) {
            var py = y + amp * S.clamp01(x / 18) * Math.sin(2 * Math.PI * x / lam - t * 5);
            if (x === 0)
                g.moveTo(x0 + x, py);
            else
                g.lineTo(x0 + x, py);
        }
        g.stroke();
    }
    var ts = x0 + fw + 9;
    if (ts < x1 - 10) {
        g.strokeStyle = S.css(S.outlineVar, a);
        g.beginPath();
        g.moveTo(ts, y);
        g.lineTo(x1 - 10, y);
        g.stroke();
    }
    g.fillStyle = S.css(col, a);
    g.beginPath();
    g.ellipse(x1 - lw / 2 - 0.25, y - lw / 2 - 0.25, lw + 0.5, lw + 0.5);
    g.fill();
}
