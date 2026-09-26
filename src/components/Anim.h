// src/components/Anim.h  —  Aether shell
// Native (Win32/DX11/ImGui) port of Caelestia's animation system.
// Source of truth: caelestia-dots/shell  components/Anim.qml + plugin/src/Caelestia/Config/{anim.cpp,tokens.hpp}
// Aether is a GPL-3.0 derivative of Caelestia Shell.
//
// Material-3-Expressive motion: cubic-bezier curves from (0,0)->c1->c2->(1,1). The "spatial"
// curves overshoot (c1.y > 1) which is the signature springy feel. Durations are in ms and are
// the EXACT values from Caelestia's AnimDurationTokens.
#pragma once

namespace Cael {
    // durations (ms) — AnimDurationTokens
    enum { DUR_SMALL=200, DUR_NORMAL=400, DUR_LARGE=600, DUR_XLARGE=1000,
           DUR_FAST_SPATIAL=350, DUR_DEFAULT_SPATIAL=500, DUR_SLOW_SPATIAL=650,
           DUR_FAST_EFFECTS=150, DUR_DEFAULT_EFFECTS=200, DUR_SLOW_EFFECTS=300 };
    // curve ids — AnimCurves
    enum Curve { STANDARD, STANDARD_ACCEL, STANDARD_DECEL, EMPHASIZED, EMPHASIZED_ACCEL, EMPHASIZED_DECEL,
                 FAST_SPATIAL, DEFAULT_SPATIAL, SLOW_SPATIAL, FAST_EFFECTS, DEFAULT_EFFECTS, SLOW_EFFECTS,
                 LINEAR, BOUNCE, CUSTOM, STRIVE };   // Aether additions: user-selectable motion styles
    // one cubic-bezier easing segment: control pts c1,c2 (ends fixed at 0,0 and 1,1). Solve x(u)=x, return y(u).
    static float bez1(float c1x,float c1y,float c2x,float c2y,float x){
        if(x<=0)return 0; if(x>=1)return 1;
        auto cx=[&](float u){ float m=1-u; return 3*m*m*u*c1x+3*m*u*u*c2x+u*u*u; };
        auto cy=[&](float u){ float m=1-u; return 3*m*m*u*c1y+3*m*u*u*c2y+u*u*u; };
        float u=x;
        for(int i=0;i<10;i++){ float e=cx(u)-x; if(fabsf(e)<1e-4f)break;
            float d=3*(1-u)*(1-u)*c1x+6*(1-u)*u*(c2x-c1x)+3*u*u*(1-c2x);
            if(fabsf(d)<1e-6f)break; u-=e/d; if(u<0)u=0; if(u>1)u=1; }
        return cy(u);
    }
    // the Material-3 "emphasized" curve = two cubic segments joined at (1/6, 0.4).
    static float emphSeg(float x,float p0x,float p0y,float c1x,float c1y,float c2x,float c2y,float p1x,float p1y){
        auto bx=[&](float u){ float m=1-u; return m*m*m*p0x+3*m*m*u*c1x+3*m*u*u*c2x+u*u*u*p1x; };
        auto by=[&](float u){ float m=1-u; return m*m*m*p0y+3*m*m*u*c1y+3*m*u*u*c2y+u*u*u*p1y; };
        float u=(p1x-p0x)>1e-5f?(x-p0x)/(p1x-p0x):0; if(u<0)u=0; if(u>1)u=1;
        for(int i=0;i<12;i++){ float e=bx(u)-x; if(fabsf(e)<1e-4f)break;
            float d=3*(1-u)*(1-u)*(c1x-p0x)+6*(1-u)*u*(c2x-c1x)+3*u*u*(p1x-c2x);
            if(fabsf(d)<1e-6f)break; u-=e/d; if(u<0)u=0; if(u>1)u=1; }
        return by(u);
    }
    static float emphasized(float x){
        if(x < 1.0f/6.0f) return emphSeg(x, 0,0, 0.05f,0, 2.f/15.f,0.06f, 1.f/6.f,0.4f);
        return emphSeg(x, 1.f/6.f,0.4f, 5.f/24.f,0.82f, 0.25f,1.f, 1.f,1.f);
    }
    // Guilty Gear Strive: SLAM past the target in the first ~fifth of the time, FREEZE there (hit-stop - the
    // frames a fighting game holds on impact so the hit reads), then snap back onto the target.
    static float strive(float x){
        const float SLAM=0.22f, HOLD=0.40f, PEAK=1.10f;
        if(x<SLAM){ float t=x/SLAM; float m=1-t; return PEAK*(1-m*m*m); }
        if(x<HOLD) return PEAK;
        float t=(x-HOLD)/(1-HOLD); float m=1-t; return PEAK+(1-PEAK)*(1-m*m);
    }
    static float eval(int c,float x){
        if(x<=0)return 0; if(x>=1)return 1;
        switch(c){
            case STANDARD:        return bez1(0.2f,0.f, 0.f,1.f, x);
            case STANDARD_ACCEL:  return bez1(0.3f,0.f, 1.f,1.f, x);
            case STANDARD_DECEL:  return bez1(0.f,0.f, 0.f,1.f, x);
            case EMPHASIZED:      return emphasized(x);
            case EMPHASIZED_ACCEL:return bez1(0.3f,0.f, 0.8f,0.15f, x);
            case EMPHASIZED_DECEL:return bez1(0.05f,0.7f, 0.1f,1.f, x);
            case FAST_SPATIAL:    return bez1(0.42f,1.67f, 0.21f,0.9f, x);
            case DEFAULT_SPATIAL: return bez1(0.38f,1.21f, 0.22f,1.f, x);   // signature springy overshoot
            case SLOW_SPATIAL:    return bez1(0.39f,1.29f, 0.35f,0.98f, x);
            case FAST_EFFECTS:    return bez1(0.31f,0.94f, 0.34f,1.f, x);
            case DEFAULT_EFFECTS: return bez1(0.34f,0.8f, 0.34f,1.f, x);
            case SLOW_EFFECTS:    return bez1(0.34f,0.88f, 0.34f,1.f, x);
            case LINEAR:          return x;
            case BOUNCE:          return 1.0f - (float)exp(-6.0*x)*(float)cos(x*10.0);   // damped spring, ~3 visible wobbles
            case STRIVE:          return strive(x);
        }
        return x;
    }
    // evaluate any curve, including a CUSTOM cubic-bezier given by its two control points
    static float evalC(int c,float x,float b0,float b1,float b2,float b3){
        if(c==CUSTOM){ if(x<=0)return 0; if(x>=1)return 1; return bez1(b0,b1,b2,b3,x); }
        return eval(c,x);
    }
    // time-based animator: eases the stored value toward `target` over `durMs` using curve `c`.
    // Re-targets from the CURRENT value whenever target changes, so it never snaps. (== QML Behavior on <prop> { Anim {} })
    struct St{ float from=0,to=0,cur=0; ULONGLONG start=0; int dur=1; int curve=0; bool init=false; float b[4]={0,0,1,1};
               ULONGLONG lastSet=0; };   // lastSet: when this value was last given a new target
    static std::unordered_map<int,St> g_st;

    // ---- rapid input: SPEED UP, never drop ---------------------------------------------------
    // Retargeting from the current value (above) means a second keypress never snaps - but on its
    // own it also means the second press hands the animation a FRESH FULL DURATION from wherever it
    // had got to. Hammer the key and the finish line keeps moving away: the motion crawls around
    // the middle and visibly never arrives, which is the "it lags and doesn't finish" feel, and the
    // usual cure - a cooldown that ignores presses - is worse, because now the shell is ignoring
    // you.
    //
    // So a retarget that lands while the previous one is still running gets a SHORTER duration, in
    // proportion to how soon it came. Press twice quickly and the first leg is compressed so it
    // arrives and the second starts crisp; flick through five and you get five animations, each
    // quicker than the last, instead of one long smear. The floor keeps the quickest of them a
    // movement rather than a teleport.
    //
    // This is the rule the workspace slide already used (WorkspaceSlide.h); it lives here now so
    // every animated value in the shell gets it, not just that one.
    static bool  g_speedUp=true;      // motion.speed_up
    static float g_speedUpFloor=0.25f;// motion.speed_up_floor - shortest allowed fraction
    static int   compressDur(const St& a, ULONGLONG now, int dur){
        if(!g_speedUp || !a.lastSet || dur<=1) return dur;
        ULONGLONG win = (ULONGLONG)dur*2;             // "still warm" window
        ULONGLONG gap = now>a.lastSet ? now-a.lastSet : 0;
        if(gap >= win) return dur;                    // unhurried: full length
        float k = (float)gap/(float)win;
        float lo = g_speedUpFloor<0.02f?0.02f:(g_speedUpFloor>1.0f?1.0f:g_speedUpFloor);
        if(k < lo) k = lo;
        int d = (int)(dur*k);
        return d<1?1:d;
    }
    // Global motion speed. HIGHER = faster (Reduce motion parks it at 9, i.e. near-instant), so a
    // duration is DIVIDED by it. This lived only in main.cpp's Approach() helper, which meant the
    // Settings "Animation speed" slider moved a handful of lerps and left the entire Material-3
    // motion system - every hover, flyout, panel morph and tab transition - running at fixed speed.
    // motion.strive_fps: the strive curve advances in steps at this rate (Arc System Works animates its
    // characters at ~12-20 fps so they read as drawn, not tweened). 0 = smooth.
    static int   g_striveFps=15;
    static inline float elapsedFor(const St& a,ULONGLONG now){
        float el=(float)(now-a.start);
        if(a.curve==STRIVE && g_striveFps>0 && el<(float)a.dur){ float step=1000.0f/(float)g_striveFps; el=floorf(el/step)*step; }
        return el;
    }
    static float g_animSpeed=1.0f;
    static inline int scaleDur(int durMs){
        float sp = g_animSpeed>0.01f ? g_animSpeed : 1.0f;
        int d=(int)(durMs/sp); return d<1?1:d;
    }
    static float anim(int id,float target,int durMs,int c){
        St& a=g_st[id]; ULONGLONG now=GetTickCount64(); durMs=scaleDur(durMs);
        if(!a.init){ a.init=true; a.cur=a.from=a.to=target; a.start=now; a.dur=durMs; a.curve=c; a.lastSet=now; return target; }
        if(fabsf(target-a.to)>1e-4f){ int d=compressDur(a,now,durMs);
                                      a.from=a.cur; a.to=target; a.start=now; a.dur=d<1?1:d; a.curve=c; a.lastSet=now; }
        float p=std::clamp(elapsedFor(a,now)/(float)a.dur,0.0f,1.0f);
        a.cur=a.from+(a.to-a.from)*eval(a.curve,p);
        return a.cur;
    }
    // same as anim(), with any curve (custom bezier control points in b0..b3)
    static float animX(int id,float target,int durMs,int c,float b0,float b1,float b2,float b3){
        St& a=g_st[id]; ULONGLONG now=GetTickCount64(); durMs=scaleDur(durMs);
        if(!a.init){ a.init=true; a.cur=a.from=a.to=target; a.start=now; a.dur=durMs; a.curve=c; a.lastSet=now; return target; }
        if(fabsf(target-a.to)>1e-4f){ int d=compressDur(a,now,durMs);
                                      a.from=a.cur; a.to=target; a.start=now; a.dur=d<1?1:d; a.curve=c; a.lastSet=now;
                                      a.b[0]=b0; a.b[1]=b1; a.b[2]=b2; a.b[3]=b3; }
        float p=std::clamp(elapsedFor(a,now)/(float)a.dur,0.0f,1.0f);
        a.cur=a.from+(a.to-a.from)*evalC(a.curve,p,a.b[0],a.b[1],a.b[2],a.b[3]);
        return a.cur;
    }
    static float current(int id){ auto it=g_st.find(id); return it==g_st.end()? 0.0f : it->second.cur; }
    static bool  known(int id){ auto it=g_st.find(id); return it!=g_st.end() && it->second.init; }
}
// hover state-layer fade — Caelestia's expressiveDefaultEffects curve @200ms (StateLayer)
static float HoverAnim(int id,bool h){ return Cael::anim(id,h?1.0f:0.0f,Cael::DUR_DEFAULT_EFFECTS,Cael::DEFAULT_EFFECTS); }
