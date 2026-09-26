// Aether - shell / dashboard tabs.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= shell / tabs
// g_tabX animates in *slot* space (position among the enabled tabs), not tab index
static int TabSlot(){ return (g_tab>=0 && g_tab<(int)g_tabs.size())? g_tab : 0; }

// tab glyphs, drawn above the label like the reference
static void TabIcon(ImDrawList* dl, int idx, ImVec2 c, ImU32 col){
    switch(idx){
    case 0: {   // dashboard: 2x2 grid, top-left tile taller
        dl->AddRectFilled(V(c.x-7,c.y-7),V(c.x-1,c.y+1),col,1.5f);
        dl->AddRectFilled(V(c.x+1,c.y-7),V(c.x+7,c.y-3),col,1.5f);
        dl->AddRectFilled(V(c.x+1,c.y-1),V(c.x+7,c.y+7),col,1.5f);
        dl->AddRectFilled(V(c.x-7,c.y+3),V(c.x-1,c.y+7),col,1.5f); } break;
    case 1: {   // media: playlist lines + note
        for(int i=0;i<3;i++) dl->AddRectFilled(V(c.x-8,c.y-6+i*4),V(c.x+1,c.y-5+i*4),col,0.5f);
        dl->AddRectFilled(V(c.x+5,c.y-8),V(c.x+6.5f,c.y+4),col,0.5f);
        dl->AddCircleFilled(V(c.x+4,c.y+5),2.6f,col); } break;
    case 2: {   // performance: gauge with a needle
        dl->PathArcTo(c,7.5f,3.6f,-0.45f,24); dl->PathStroke(col,0,1.8f);
        dl->AddLine(c,V(c.x+4.5f,c.y-5.0f),col,1.8f);
        dl->AddCircleFilled(c,1.8f,col); } break;
    default: {  // weather: cloud
        dl->AddCircleFilled(V(c.x-3,c.y+1),4.2f,col);
        dl->AddCircleFilled(V(c.x+3,c.y-1),5.2f,col);
        dl->AddRectFilled(V(c.x-4,c.y+1),V(c.x+6,c.y+5),col,2.0f); } break;
    }
}
