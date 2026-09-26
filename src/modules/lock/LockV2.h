// src/modules/lock/LockV2.h  —  Aether shell
// The newer Caelestia lock screen (reference\v2\NOTES.md §11): one rounded surface with three columns.
//   left   - weather card (condition, big temperature + glyph, feels like, high / low), a fetch card
//            (distro logo, OS / WM / USER / UP, palette dots), a mini player
//   centre - a huge condensed clock (hours big, minutes stacked beside a PM chip), "TUESDAY • 9 JUN", the avatar,
//            the password pill
//   right  - resource badges in M3 shapes that fill up from the bottom (CPU pentagon with a temperature bubble,
//            memory square, storage gem), then a notifications card
// lock.style = "caelestia". Input, Hello and unlocking stay in DrawLock's shared code; this only sets g_lockUbh.
#pragma once

static void V2EmptyScene(ImDrawList* dl,ImVec2 c,float px,ImU32 col);   // fwd (SidebarV2.h)

static void DrawLockV2(ImDrawList* dl,ImGuiIO& io,float W,float H,float a,float content,bool errNow,float shakeX,float dotPop){
    const float S=1.0f/std::max(0.5f,g_uiScale);
    auto A=[&](ImU32 c,float t){ return MulA(c,std::clamp(t,0.0f,1.0f)); };
    auto stg=[&](int i){ return Cael::eval(Cael::DEFAULT_SPATIAL,std::clamp((content-i*0.07f)/0.6f,0.0f,1.0f)); };
    time_t nn=time(nullptr); struct tm lt; localtime_s(&lt,&nn);
    ImFont* mono=g_fMono? g_fMono : g_fMed;
    ImFont* cond=g_fCond? g_fCond : g_fMed;
    ImU32 surf=Mix(PanelCol(255),IM_COL32(0,0,0,255),g_darkUI? 0.25f : 0.0f);
    ImU32 card=Mix(PanelCol(255),COL_CARD2,0.45f);
    ImU32 prim=Mix(COL_INK,COL_GOLD,0.55f);
    bool click=io.MouseClicked[0] && content>0.8f;

    // ---- the surface grows out of the centre ----
    float pw=std::min(W*0.72f,1260.0f*S), ph=std::min(H*0.80f,800.0f*S);
    float gw=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((a-0.04f)/0.45f,0.0f,1.0f));
    float gh=Cael::eval(Cael::EMPHASIZED_DECEL,std::clamp((a-0.20f)/0.50f,0.0f,1.0f));
    float S0=110*S, cw=S0+(pw-S0)*gw, ch=S0+(ph-S0)*gh;
    float x0=(W-pw)*0.5f, y0=(H-ph)*0.5f;
    float gx=(W-cw)*0.5f, gy=(H-ch)*0.5f;
    float rnd=std::min(28*S+(S0*0.5f-28*S)*(1.0f-gh),ch*0.5f);
    for(int i=8;i>0;i--){ float e=i*3.0f*S; dl->AddRect(V(gx-e,gy-e+5*S),V(gx+cw+e,gy+ch+e+5*S),A(IM_COL32(0,0,0,9),a),rnd+e,0,e); }
    dl->AddRectFilled(V(gx,gy),V(gx+cw,gy+ch),A(WithA(surf,240),std::min(1.0f,a*4)),rnd);
    dl->PushClipRect(V(gx,gy),V(gx+cw,gy+ch),true);

    const float P=16*S, G=12*S;
    float colL=pw*0.25f, colR=pw*0.25f;
    float lx=x0+P, rx=x0+pw-P-colR, cxm=x0+pw*0.5f;

    // ================= left =================
    // weather
    { float e=stg(1), ox=(1.0f-e)*-30*S;
      ImVec2 a0=V(lx+ox,y0+P), b0=V(lx+colL+ox,y0+P+200*S);
      dl->AddRectFilled(a0,b0,A(card,e),20*S);
      float cx=(a0.x+b0.x)*0.5f;
      if(g_wx.ok){
          const char* cond2=WxText(g_wx.code);
          TextAt(dl,g_fSml,17*S,V(cx-TextW(g_fSml,17*S,cond2)*0.5f,a0.y+18*S),A(COL_INK,e),cond2);
          char t[16]; snprintf(t,16,"%d\xC2\xB0""C",(int)std::round(g_wx.temp));
          float tf=56*S, tw=TextW(g_fMed,tf,t), iw=50*S;
          float sx=cx-(tw+iw+8*S)*0.5f;
          TextAt(dl,g_fMed,tf,V(sx,a0.y+44*S),A(prim,e),t);
          MsIcon(dl,WxMaterial(g_wx.code,!g_wx.isDay),V(sx+tw+8*S+iw*0.5f,a0.y+44*S+tf*0.55f),iw,A(prim,e));
          char f1[48]; snprintf(f1,48,"Feels like %d\xC2\xB0""C",(int)std::round(g_wx.feels));
          TextAt(dl,g_fSml,17*S,V(cx-TextW(g_fSml,17*S,f1)*0.5f,a0.y+122*S),A(COL_INK,e),f1);
          if(!g_wx.dMax.empty() && !g_wx.dMin.empty()){
              char f2[64]; snprintf(f2,64,"High %d\xC2\xB0""C \xE2\x80\xA2 Low %d\xC2\xB0""C",(int)std::round(g_wx.dMax[0]),(int)std::round(g_wx.dMin[0]));
              TextAt(dl,g_fSml,16*S,V(cx-TextW(g_fSml,16*S,f2)*0.5f,a0.y+148*S),A(COL_INK2,e),f2); }
      } else TextAt(dl,g_fSml,16*S,V(cx-TextW(g_fSml,16*S,"Weather\xE2\x80\xA6")*0.5f,a0.y+90*S),A(COL_INK2,e),"Weather\xE2\x80\xA6");
    }
    // fetch
    { float e=stg(2), ox=(1.0f-e)*-30*S;
      ImVec2 a0=V(lx+ox,y0+P+200*S+G), b0=V(lx+colL+ox,y0+P+200*S+G+250*S);
      dl->AddRectFilled(a0,b0,A(card,e),20*S);
      ImVec2 hi=V(a0.x+22*S,a0.y+24*S);
      dl->AddRectFilled(V(hi.x-10*S,hi.y-10*S),V(hi.x+10*S,hi.y+10*S),A(Mix(COL_CARD2,COL_INK2,0.25f),e),5*S);
      MsIcon(dl,"terminal",hi,15*S,A(COL_INK,e));
      TextAt(dl,mono,16*S,V(a0.x+42*S,a0.y+14*S),A(COL_INK,e),"aetherfetch.ps1");
      float logoR=46*S; ImVec2 lc=V(a0.x+18*S+logoR,a0.y+110*S);
      if(!V2LogoMask(dl,ExeDir()+"assets\\logos\\windows-11.png",lc,logoR*1.7f,A(prim,e))) MsIcon(dl,"window",lc,logoR*1.6f,A(prim,e));
      static std::string user; if(user.empty()) user=ProfileDisplayName();
      std::string up=ProfileUptimeLong(); if(up.rfind("up ",0)==0) up=up.substr(3);
      std::string rows[4][2]={ {"OS  ",ProfileOsName()},{"WM  ",ProfileWmName()},{"USER",user},{"UP  ",up} };
      float tx=lc.x+logoR+14*S, tw=b0.x-14*S-tx;
      for(int i=0;i<4;i++){
          float ry=a0.y+64*S+i*25*S;
          std::string line=rows[i][0]+": "+rows[i][1];
          TextAt(dl,mono,16*S,V(tx,ry),A(COL_INK,e),Clip(mono,16*S,line,tw).c_str());
      }
      ImU32 dots[8]={ COL_GOLD, M3Secondary(), M3Tertiary(), Mix(COL_GOLD,IM_COL32(120,220,210,255),0.5f),
                      Mix(M3Secondary(),IM_COL32(140,160,255,255),0.5f), Mix(COL_GOLD,IM_COL32(150,200,255,255),0.5f), COL_INK2, COL_INK };
      float dr=13*S, dg=10*S; float dx=a0.x+(colL-(8*dr*2+7*dg))*0.5f+dr;
      for(int i=0;i<8;i++) dl->AddRectFilled(V(dx+i*(dr*2+dg)-dr,b0.y-38*S-dr),V(dx+i*(dr*2+dg)+dr,b0.y-38*S+dr),A(dots[i],e),6*S);
    }
    // mini player
    { float e=stg(3), ox=(1.0f-e)*-30*S;
      float top=y0+P+200*S+G+250*S+G, bot=y0+ph-P;
      if(bot-top>90*S){
          float cx=lx+colL*0.5f+ox, cy=(top+bot)*0.5f;
          bool has=g_md.has && !g_md.title.empty();
          if(g_mdArt && has){   // the cover, faint, behind the player - kept inside the column
              ImVec2 ra=V(lx+ox,top), rb=V(lx+colL+ox,bot); ImVec2 uv0,uv1; CoverUV(g_mdArtW>0?g_mdArtW:1,g_mdArtH>0?g_mdArtH:1,rb.x-ra.x,rb.y-ra.y,uv0,uv1);
              dl->AddImageRounded((ImTextureID)g_mdArt,ra,rb,uv0,uv1,A(IM_COL32(255,255,255,46),e),20*S);
              dl->AddRectFilled(ra,rb,A(WithA(PanelCol(255),150),e),20*S); }
          std::string t1=has? Clip(g_fMed,19*S,g_md.title,colL-10) : std::string("Nothing playing");
          TextAt(dl,g_fMed,19*S,V(cx-TextW(g_fMed,19*S,t1.c_str())*0.5f,cy-40*S),A(prim,e),t1.c_str());
          if(has){ std::string t2=Clip(g_fSml,15*S,g_md.artist,colL-10); TextAt(dl,g_fSml,15*S,V(cx-TextW(g_fSml,15*S,t2.c_str())*0.5f,cy-17*S),A(COL_INK,e),t2.c_str()); }
          float bs=34*S;
          if(V2Btn(dl,io,V(cx-bs*1.35f,cy+22*S),bs,bs,"skip_previous",false,0x6A000,click)) g_reqPrev=1;
          if(V2Btn(dl,io,V(cx,cy+22*S),bs*1.6f,bs,g_md.playing? "pause" : "play_arrow",true,0x6A001,click,10*S)) g_reqPlay=1;
          if(V2Btn(dl,io,V(cx+bs*1.35f,cy+22*S),bs,bs,"skip_next",false,0x6A002,click)) g_reqNext=1;
      }
    }

    // ================= centre =================
    { float e=stg(0), rise=(1.0f-e)*26*S;
      ImFont* cond=g_fCondBig? g_fCondBig : (g_fCond? g_fCond : g_fMed);   // big clock digits: the 256px bake
      int h12=g_clock24? lt.tm_hour : ((lt.tm_hour%12)==0? 12 : lt.tm_hour%12);
      char hh[8], mm[8]; snprintf(hh,8,"%02d",h12); snprintf(mm,8,"%02d",lt.tm_min);
      float hf=230*S, mf=110*S;
      float hw=TextW(cond,hf,hh), mw=TextW(cond,mf,mm);
      const float squeezeK=0.72f; float total=hw*squeezeK+14*S+mw*squeezeK, sx=cxm-total*0.5f, ty=y0+P+20*S+rise;
      // condensed look: squeeze the glyphs horizontally around their own left edge
      int v0=dl->VtxBuffer.Size;
      TextAt(dl,cond,hf,V(sx,ty),A(prim,e),hh);
      float squeeze=0.72f;
      for(int i=v0;i<dl->VtxBuffer.Size;i++) dl->VtxBuffer[i].pos.x = sx+(dl->VtxBuffer[i].pos.x-sx)*squeeze;
      float hx1=sx+hw*squeeze;
      int v1=dl->VtxBuffer.Size;
      float mx=hx1+14*S;
      TextAt(dl,cond,mf,V(mx,ty+12*S),A(prim,e),mm);
      for(int i=v1;i<dl->VtxBuffer.Size;i++) dl->VtxBuffer[i].pos.x = mx+(dl->VtxBuffer[i].pos.x-mx)*squeeze;
      float mw2=mw*squeeze;
      if(!g_clock24){ const char* ap=lt.tm_hour<12? "AM" : "PM";
          float cf=44*S; float aw=TextW(cond,cf,ap)*squeeze+22*S;
          ImVec2 ca=V(mx,ty+mf+22*S), cb=V(mx+std::max(aw,mw2),ca.y+cf+14*S);
          dl->AddRectFilled(ca,cb,A(Mix(COL_CARD2,COL_INK2,0.18f),e),10*S);
          int v2=dl->VtxBuffer.Size; float tx=ca.x+((cb.x-ca.x)-TextW(cond,cf,ap)*squeeze)*0.5f;
          TextAt(dl,cond,cf,V(tx,ca.y+7*S),A(prim,e),ap);
          for(int i=v2;i<dl->VtxBuffer.Size;i++) dl->VtxBuffer[i].pos.x = tx+(dl->VtxBuffer[i].pos.x-tx)*squeeze; }
      static const char* DN[]={"SUNDAY","MONDAY","TUESDAY","WEDNESDAY","THURSDAY","FRIDAY","SATURDAY"};
      static const char* MN[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
      char dt[64]; snprintf(dt,64,"%s \xE2\x80\xA2 %d %s",DN[lt.tm_wday],lt.tm_mday,MN[lt.tm_mon]);
      float df=22*S; float dy=ty+hf*1.02f;
      TextAt(dl,g_fMed,df,V(cxm-TextW(g_fMed,df,dt)*0.5f,dy),A(COL_INK,e),dt);
      // avatar
      float e2=stg(2); float R=95*S; ImVec2 ac=V(cxm,dy+df+40*S+R+(1.0f-e2)*20*S);
      dl->AddCircleFilled(ac,R+2,A(card,e2),64);
      ProfileAvatar(dl,ac,R,0.0f);
      // password pill
      float e3=stg(3); float ppw=std::min(330*S,pw*0.30f), pph=48*S;
      float pcx=cxm+shakeX, pyy=ac.y+R+36*S+(1.0f-e3)*20*S;
      ImVec2 pa=V(pcx-ppw*0.5f,pyy), pb=V(pcx+ppw*0.5f,pyy+pph);
      dl->AddRectFilled(pa,pb,A(card,e3),pph*0.5f);
      if(errNow) dl->AddRect(pa,pb,A(WithA(COL_ERR,230),e3),pph*0.5f,0,1.6f);
      MsIcon(dl,"lock",V(pa.x+26*S,pyy+pph*0.5f),20*S,A(COL_INK2,e3));
      int nch=(int)strlen(g_lockPw);
      dl->PushClipRect(V(pa.x+44*S,pa.y),V(pb.x-50*S,pb.y),true);
      if(nch==0){ const char* ph2= errNow? "Wrong - try again" : "Enter your password";
          TextAt(dl,mono,16*S,V(pa.x+48*S,pyy+pph*0.5f-9*S),A(errNow? COL_ERR : COL_INK2,e3),ph2); }
      else { for(int i=0;i<nch && i<24;i++){ float pop= i==nch-1? 1.0f+0.7f*dotPop : 1.0f;
              dl->AddCircleFilled(V(pa.x+54*S+i*14*S,pyy+pph*0.5f),4.5f*S*pop,A(COL_INK,e3),12); } }
      dl->PopClipRect();
      ImVec2 ub=V(pb.x-26*S,pyy+pph*0.5f);
      g_lockUbh = fabsf(io.MousePos.x-ub.x)<20*S && fabsf(io.MousePos.y-ub.y)<20*S;
      float uh=HoverAnim(0x6A010,g_lockUbh);
      if(uh>0.01f) dl->AddCircleFilled(ub,17*S,A(WithA(COL_INK2,(int)(50*uh)),e3),20);
      MsIcon(dl,"arrow_forward",ub,24*S,A(COL_INK,e3));
      g_lockHelloBh=false;
    }

    // ================= right =================
    // resource shapes
    { float e=stg(1), ox=(1.0f-e)*30*S;
      ImVec2 a0=V(rx+ox,y0+P), b0=V(rx+colR+ox,y0+P+150*S);
      dl->AddRectFilled(a0,b0,A(card,e),20*S);
      float fr[3]={ (float)g_st.cpuUsage, g_st.memTotal? (float)((double)g_st.memUsed/g_st.memTotal) : 0.0f,
                    g_st.diskTotal? (float)((double)g_st.diskUsed/g_st.diskTotal) : 0.0f };
      static const int SH[3]={ M3_PENTAGON, M3_SQUARE, M3_GEM };
      static const char* IC[3]={ "memory","memory_alt","hard_drive_2" };
      ImU32 fills[3]={ Mix(COL_GOLD,COL_INK,0.35f), Mix(M3Tertiary(),COL_INK,0.25f), Mix(M3Secondary(),COL_INK,0.35f) };
      float cw3=colR/3.0f;
      for(int i=0;i<3;i++){
          float R=std::min(cw3*0.42f,52*S); ImVec2 c=V(a0.x+cw3*(i+0.5f),(a0.y+b0.y)*0.5f+6*S);
          float f=Cael::anim(0x6A100+i,fr[i],Cael::DUR_DEFAULT_SPATIAL*2,Cael::DEFAULT_SPATIAL);
          M3Shape(dl,c,R,A(Mix(COL_CARD2,COL_INK2,0.30f),e),SH[i],0.0f);
          float fy=c.y+R-2*R*std::clamp(f,0.0f,1.0f);
          dl->PushClipRect(V(c.x-R-2,fy),V(c.x+R+2,c.y+R+2),true);
          M3Shape(dl,c,R,A(fills[i],e),SH[i],0.0f);
          dl->PopClipRect();
          MsIcon(dl,IC[i],V(c.x,c.y-R*0.38f),R*0.34f,A(Mix(COL_CARD2,IM_COL32(0,0,0,255),0.5f),e));
          char pc[8]; snprintf(pc,8,"%d%%",(int)std::round(f*100)); float pf=R*0.46f;
          TextAt(dl,cond,pf,V(c.x-TextW(cond,pf,pc)*0.5f,c.y-pf*0.2f),A(Mix(COL_CARD2,IM_COL32(0,0,0,255),0.4f),e),pc);
          if(i==0 && g_st.cpuTemp>0){ ImVec2 tc=V(c.x+R*0.78f,c.y-R*0.78f); float tr=R*0.42f;
              dl->AddCircleFilled(tc,tr,A(Mix(COL_CARD2,COL_INK2,0.25f),e),24);
              char tt[12]; snprintf(tt,12,"%d\xC2\xB0""C",(int)std::round(g_st.cpuTemp)); float tfs=tr*0.62f;
              TextAt(dl,g_fMed,tfs,V(tc.x-TextW(g_fMed,tfs,tt)*0.5f,tc.y-tfs*0.55f),A(COL_INK,e),tt); }
      }
    }
    // notifications
    { float e=stg(2), ox=(1.0f-e)*30*S;
      ImVec2 a0=V(rx+ox,y0+P+150*S+G), b0=V(rx+colR+ox,y0+ph-P);
      dl->AddRectFilled(a0,b0,A(card,e),20*S);
      TextAt(dl,mono,15*S,V(a0.x+16*S,a0.y+14*S),A(COL_INK,e),"Notifications");
      std::vector<Notif> snap; { std::lock_guard<std::mutex> lk(g_notifMtx); snap=g_notifs; }
      std::sort(snap.begin(),snap.end(),[](const Notif& x,const Notif& y){ return x.at>y.at; });
      if(snap.empty()){
          float cy=(a0.y+b0.y)*0.5f;
          V2EmptyScene(dl,V((a0.x+b0.x)*0.5f,cy-10*S),3.2f*S,A(Mix(COL_CARD2,COL_INK2,0.55f),e));
          const char* m="No Notifications"; TextAt(dl,mono,17*S,V((a0.x+b0.x)*0.5f-TextW(mono,17*S,m)*0.5f,cy+26*S),A(COL_INK2,e),m);
      } else {
          float y=a0.y+44*S; float w=colR-32*S;
          auto flat=[](std::string t){ for(auto& c2:t) if(c2==10||c2==13||c2==9) c2=32; return t; };
          for(auto& n:snap){ if(y>b0.y-56*S) break;
              dl->AddRectFilled(V(a0.x+10*S,y),V(b0.x-10*S,y+50*S),A(Mix(card,COL_CARD2,0.5f),e),12*S);
              TextAt(dl,g_fMed,15*S,V(a0.x+22*S,y+7*S),A(COL_INK,e),Clip(g_fMed,15*S,flat(n.app.empty()? n.title : n.app),w-20*S).c_str());
              TextAt(dl,g_fSml,14*S,V(a0.x+22*S,y+27*S),A(COL_INK2,e),Clip(g_fSml,14*S,flat(n.title+"  "+n.body),w-20*S).c_str());
              y+=58*S; }
      }
    }
    dl->PopClipRect();
}
