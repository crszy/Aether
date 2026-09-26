// Aether - toasts (now playing, recording, notifications).
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// =============================================================================================
// TOASTS — one card language for every transient card in the shell: now-playing, screen
// recording and the notification stack. They used to be three different looks (the media flyout
// was even hardcoded dark, ignoring the theme). Everything now goes through ToastCard().
// =============================================================================================
static float g_recToastAnim=0.0f; static ULONGLONG g_recToastUntil=0; static bool g_recToastOpen=false;
static float g_shotToastAnim=0.0f; static ULONGLONG g_shotToastUntil=0; static std::string g_shotDir, g_shotFile; static bool g_shotOk=false;
static bool  g_recToastExpanded=false;          // clicking the toast opens the details
static RECT  g_medRect={0,0,0,0};               // the toast window's clickable area
static void ShowRecordingToast(){
    g_recToastUntil=GetTickCount64()+ (g_recording? 6500 : 5000);
    g_recToastExpanded=false;
    if(g_medWake) PostMessageW(g_medWake,WM_NULL,0,0);
}
// shadow + glass + accent edge + sheen: the shared toast body
// Draws the cover inside a box KEEPING ITS ASPECT: the whole image is shown, centred, on a tinted
// backdrop so the spare edges read as part of the frame. Video thumbnails are 16:9 and used to be
// squashed into the square frames.
static void DrawArtFit(ImDrawList* dl,ImVec2 a,ImVec2 b,float rnd,int alpha,ImU32 backdrop){
    float bw=b.x-a.x, bh=b.y-a.y;
    if(bw<=0||bh<=0) return;
    dl->AddRectFilled(a,b,WithA(backdrop,(int)(alpha*0.55f)),rnd);
    if(!g_mdArt){ return; }
    float sw=(float)(g_mdArtW>0?g_mdArtW:1), sh=(float)(g_mdArtH>0?g_mdArtH:1);
    float sc=std::min(bw/sw,bh/sh);
    float w=sw*sc, h=sh*sc;
    ImVec2 c=V((a.x+b.x)*0.5f,(a.y+b.y)*0.5f);
    dl->PushClipRect(a,b,true);
    dl->AddImageRounded((ImTextureID)g_mdArt,V(c.x-w*0.5f,c.y-h*0.5f),V(c.x+w*0.5f,c.y+h*0.5f),
                        ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,alpha),
                        (w>=bw-1.0f&&h>=bh-1.0f)? rnd : rnd*0.5f);
    dl->PopClipRect();
}
static void ToastCard(ImDrawList* dl,ImVec2 a,ImVec2 b,float alpha,ImU32 accent,float rnd=18.0f){
    int A8=(int)(std::clamp(alpha,0.0f,1.0f)*255.0f);
    auto M=[&](ImU32 c,int mul){ return MulA(WithA(c,mul),std::clamp(alpha,0.0f,1.0f)); };
    for(int i=6;i>0;i--){ float e=i*1.6f;
        dl->AddRectFilled(V(a.x-e*0.4f,a.y+e*0.55f),V(b.x+e*0.4f,b.y+e*0.75f),
                          IM_COL32(0,0,0,(int)(9*alpha)),rnd+e); }
    dl->AddRectFilled(a,b,MulA(PanelCol(248),std::clamp(alpha,0.0f,1.0f)),rnd);
    // a soft accent wash down the left edge, clipped to the rounded body
    dl->PushClipRect(a,V(a.x+rnd*2.4f,b.y),true);
    dl->AddRectFilled(a,b,M(accent,g_darkUI?52:38),rnd);
    dl->PopClipRect();
    dl->AddRectFilled(V(a.x,a.y+rnd*0.5f),V(a.x+3.5f,b.y-rnd*0.5f),MulA(accent,std::clamp(alpha,0.0f,1.0f)),2.0f);
    // top sheen (fading, not clipped to a hard line) + rim
    TopWash(dl,a,b,(int)((g_darkUI?14:22)*alpha),rnd,0,0.65f);
    dl->AddRect(a,b,IM_COL32(255,255,255,(int)((g_darkUI?26:120)*alpha)),rnd,0,1.2f);
    dl->AddRect(V(a.x+0.5f,a.y+0.5f),V(b.x-0.5f,b.y-0.5f),IM_COL32(0,0,0,(int)(38*alpha)),rnd,0,1.0f);
    (void)A8;
}
// a small round icon button used on the toasts
static bool ToastBtn(ImDrawList* dl,ImVec2 c,float r,int kind,float alpha,ImGuiIO& io,int id){
    bool hov = fabsf(io.MousePos.x-c.x)<r+3 && fabsf(io.MousePos.y-c.y)<r+3;
    float ha=HoverAnim(id,hov);
    dl->AddCircleFilled(c,r+ha*1.5f,MulA(WithA(COL_INK2,(int)(28+ha*36)),alpha));
    ImU32 col=MulA(COL_INK,alpha);
    float s=r*0.52f;
    if(kind==0){ dl->AddTriangleFilled(V(c.x+s*0.8f,c.y-s),V(c.x+s*0.8f,c.y+s),V(c.x-s*0.7f,c.y),col);
                 dl->AddRectFilled(V(c.x-s*1.1f,c.y-s),V(c.x-s*0.75f,c.y+s),col,1); }
    else if(kind==2){ dl->AddTriangleFilled(V(c.x-s*0.8f,c.y-s),V(c.x-s*0.8f,c.y+s),V(c.x+s*0.7f,c.y),col);
                      dl->AddRectFilled(V(c.x+s*0.75f,c.y-s),V(c.x+s*1.1f,c.y+s),col,1); }
    else if(kind==1){ if(g_md.playing){ dl->AddRectFilled(V(c.x-s*0.8f,c.y-s),V(c.x-s*0.15f,c.y+s),col,1.5f);
                                        dl->AddRectFilled(V(c.x+s*0.15f,c.y-s),V(c.x+s*0.8f,c.y+s),col,1.5f); }
                      else dl->AddTriangleFilled(V(c.x-s*0.7f,c.y-s),V(c.x-s*0.7f,c.y+s),V(c.x+s*0.9f,c.y),col); }
    else if(kind==3){ dl->AddLine(V(c.x-s,c.y-s),V(c.x+s,c.y+s),col,2.0f);      // close
                      dl->AddLine(V(c.x+s,c.y-s),V(c.x-s,c.y+s),col,2.0f); }
    else if(kind==4){ dl->AddRect(V(c.x-s,c.y-s*0.7f),V(c.x+s,c.y+s),col,2.0f,0,1.6f);  // folder
                      dl->AddLine(V(c.x-s,c.y-s*0.7f),V(c.x-s*0.2f,c.y-s*1.2f),col,1.6f); }
    return hov;
}
// clip a string to a pixel width, with an ellipsis
static std::string Clip(ImFont* f,float sz,const std::string& in,float w){
    if(TextW(f,sz,in.c_str())<=w) return in;
    std::string s=in;
    while(s.size()>1 && TextW(f,sz,(s+"\xE2\x80\xA6").c_str())>w) s.pop_back();
    return s+"\xE2\x80\xA6";
}
