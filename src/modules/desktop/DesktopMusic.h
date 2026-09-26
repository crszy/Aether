// src/modules/desktop/DesktopMusic.h  —  Aether shell
// Two things the newer Caelestia rice paints straight onto the wallpaper (reference\v2\NOTES.md §10):
//   * desktop lyrics - the current line of the playing song, bold, with the previous / next lines dim around it,
//     sliding up as the song moves on (the lines come from the same synced-lyrics fetch as the Media tab)
//   * a background visualiser - thin bars along the bottom edge that follow the audio
// Each can hide itself while a window covers that screen, and every size / position is a setting (desktop.*).
#pragma once

static void AudioBands(float* out,int n,bool playing);                 // fwd
static int  LyricCurrent(const std::vector<LyricLine>& L,double t,float& p);   // fwd

// is an app window covering a good part of monitor `mi`? (cached; the desk layer asks every frame)
static bool DeskMonCovered(int mi){
    static ULONGLONG at[16]={0}; static bool val[16]={false};
    if(mi<0||mi>=16||mi>=(int)g_mons.size()) return false;
    ULONGLONG t=GetTickCount64(); if(at[mi] && t-at[mi]<400) return val[mi]; at[mi]=t;
    struct Ctx{ RECT mon; bool hit; DWORD self; } c{ g_mons[mi].rc, false, GetCurrentProcessId() };
    EnumWindows([](HWND h,LPARAM lp)->BOOL{
        Ctx& c=*(Ctx*)lp;
        if(!IsWindowVisible(h)||IsIconic(h)) return TRUE;
        LONG_PTR ex=GetWindowLongPtrW(h,GWL_EXSTYLE); if(ex&WS_EX_TOOLWINDOW) return TRUE;
        if(GetWindow(h,GW_OWNER)) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(h,&pid); if(pid==c.self) return TRUE;
        BOOL cloaked=FALSE; DwmGetWindowAttribute(h,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)); if(cloaked) return TRUE;
        wchar_t cls[64]; GetClassNameW(h,cls,64);
        if(!wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW")||!wcscmp(cls,L"Shell_TrayWnd")) return TRUE;
        RECT r, in; if(!GetWindowRect(h,&r)||!IntersectRect(&in,&r,&c.mon)) return TRUE;
        double area=(double)(in.right-in.left)*(in.bottom-in.top), monA=(double)(c.mon.right-c.mon.left)*(c.mon.bottom-c.mon.top);
        if(monA>0 && area/monA>0.30){ c.hit=true; return FALSE; }
        return TRUE; },(LPARAM)&c);
    val[mi]=c.hit; return c.hit;
}

// the desk layer only redraws when dirty: keep it ticking while there is something moving on it
static void DeskMusicTick(){
    if(!(g_deskLyrics||g_deskViz)) return;
    static ULONGLONG last=0; static bool wasPlaying=false;
    bool playing=g_md.has && g_md.playing;
    ULONGLONG t=GetTickCount64();
    if(playing && t-last>=(ULONGLONG)std::max(8,1000/std::clamp(g_deskMusicFps,10,120))){ last=t; g_deskDirty=true; }
    if(playing!=wasPlaying){ wasPlaying=playing; g_deskDirty=true; }
    if(g_deskLyrics && playing) LyricsMaybeFetch();
}

static void DrawDesktopMusic(ImDrawList* dl,int mi,float x0,float y0,float x1,float y1){
    bool covered=DeskMonCovered(mi);
    float W=x1-x0, H=y1-y0;
    bool playing=g_md.has && g_md.playing;
    float dt=std::min(g_frameDt,0.1f);

    // ---- visualiser ----
    { static float fade=0; bool want=g_deskViz && playing && !(g_deskVizAutoHide && covered);
      fade+= ((want? 1.0f : 0.0f)-fade)*std::min(1.0f,dt*6.0f);
      if(fade>0.01f){
          const float barW=std::max(1.0f,g_deskVizBar), gap=std::max(0.0f,g_deskVizGap);
          int N=std::clamp((int)(W/(barW+gap)),8,512);
          static float bands[256]; int nb=std::clamp(N/2+1,8,256);
          AudioBands(bands,nb,playing);
          static std::vector<float> smooth; if((int)smooth.size()!=N) smooth.assign(N,0.0f);
          float maxH=H*std::clamp(g_deskVizHeight,0.02f,0.9f);
          ImU32 col = g_deskVizColor.empty()? WithA(COL_INK,(int)(150*fade)) : MulA(CwColor(g_deskVizColor,COL_INK),fade);
          float total=N*(barW+gap)-gap, sx=x0+(W-total)*0.5f;
          for(int i=0;i<N;i++){
              int d = std::abs(i-(N-1)/2);                                     // mirrored: bass at both edges
              int bi = g_deskVizMirror? std::clamp((N/2-d)*(nb-1)/std::max(1,N/2),0,nb-1) : std::clamp(i*(nb-1)/std::max(1,N-1),0,nb-1);
              float v=std::clamp(bands[bi],0.0f,1.0f);
              float& s=smooth[i]; s += (v-s)*std::min(1.0f,dt*(v>s? 22.0f : 7.0f));
              float h=std::max(barW,s*maxH);
              float bx=sx+i*(barW+gap);
              dl->AddRectFilled(V(bx,y1-h),V(bx+barW,y1),col,std::min(barW*0.5f,2.0f));
          }
      }
    }

    // ---- lyrics ----
    { static float fade=0;
      std::vector<LyricLine> L; { std::lock_guard<std::mutex> lk(g_lyricsMtx); L=g_lyrics; }
      bool want=g_deskLyrics && playing && !L.empty() && !(g_deskLyricsAutoHide && covered);
      fade+= ((want? 1.0f : 0.0f)-fade)*std::min(1.0f,dt*5.0f);
      if(fade>0.01f && !L.empty()){
          float p=0; int cur=LyricCurrent(L,MediaPos(),p);
          static int lastCur=-2; static float roll=1.0f;
          if(cur!=lastCur){ lastCur=cur; roll=0.0f; }
          roll=std::min(1.0f,roll+dt*3.2f);
          float e=Cael::eval(Cael::DEFAULT_SPATIAL,roll);
          const float S=1.0f/std::max(0.5f,g_uiScale);
          float big=std::clamp(g_deskLyricsSize,12.0f,96.0f)*S, small=big*0.62f;
          float cy=y0+H*std::clamp(g_deskLyricsY,0.05f,0.95f);
          float gapL=big*1.15f;
          float maxW=W*0.62f;
          auto centred=[&](ImFont* f,float sz,const std::string& t,float y,ImU32 col){
              std::vector<std::string> ls; WrapLines(f,sz,t,maxW,2,ls);
              for(size_t k=0;k<ls.size();k++){
                  float w=TextW(f,sz,ls[k].c_str()); float yy=y+k*sz*1.12f-(ls.size()-1)*sz*0.56f;
                  // a soft shadow so a bright wallpaper does not swallow it
                  TextAt(dl,f,sz,V(x0+(W-w)*0.5f+1.5f,yy+2.0f),WithA(IM_COL32(0,0,0,255),(int)(((col>>24)&0xFF)*0.55f)),ls[k].c_str());
                  TextAt(dl,f,sz,V(x0+(W-w)*0.5f,yy),col,ls[k].c_str()); } };
          float off=(1.0f-e)*gapL;                                             // the lines slide up into place
          int al=(int)(255*fade);
          if(cur-1>=0)            centred(g_fSml,small,L[cur-1].text,cy-gapL+off,WithA(COL_INK,(int)(al*0.45f*e)));
          if(cur>=0)              centred(g_fMed,big,  L[cur].text,  cy+off,     WithA(COL_INK,al));
          if(cur+1<(int)L.size()) centred(g_fSml,small,L[cur+1].text,cy+gapL*0.95f+off,WithA(COL_INK,(int)(al*0.45f)));
      }
    }
}
