// Aether - media: WinRT now-playing.
// Split out of main.cpp. main.cpp #includes every src/app/ file in order, so the shell is still ONE
// translation unit exactly as before; this file is never compiled on its own.
#ifndef AETHER_UNITY
#error "src/app/*.cpp are #included by main.cpp - build main.cpp (build.ps1), not this file"
#endif

// ================================================================= media (WinRT now-playing, == Linux MPRIS)
struct Media { bool has=false, playing=false; std::string title,artist,album; double pos=0,dur=0; ULONGLONG posTick=0; } g_md;
// The session only reports a position when it is polled, so a ring driven straight off g_md.pos
// JUMPED once a second instead of filling. Run a local clock between reports and re-sync whenever
// the reported value actually moves: the arc then creeps round continuously and lands exactly on
// full as the track ends, which is the behaviour being copied from Caelestia.
static double MediaPos(){
    // Lyrics need the playhead to within a frame or two, not to the second. The media thread now publishes
    // a sub-second position corrected for WHEN the player measured it (posTick = when we read it), so the
    // estimate between polls is exact; the displayed clock glides onto it instead of stepping, and snaps
    // on a seek, pause or skip.
    static double disp=0.0; static ULONGLONG lastT=0; static bool lastPlay=false; static std::string lastKey;
    ULONGLONG now=GetTickCount64();
    double est = g_md.pos + ((g_md.playing && g_md.posTick)? (double)(now-g_md.posTick)/1000.0 : 0.0);
    std::string key=g_md.title+"|"+g_md.artist;
    if(lastT==0 || key!=lastKey){ disp=est; lastT=now; lastKey=key; }
    double dt=(double)(now-lastT)/1000.0; lastT=now;
    if(g_md.playing) disp+=dt;
    double err=est-disp;
    if(fabs(err)>0.75 || g_md.playing!=lastPlay) disp=est;
    else disp+=err*std::min(1.0,dt*3.0);
    lastPlay=g_md.playing;
    if(g_md.dur>0.0) disp=std::clamp(disp,0.0,g_md.dur);
    return std::max(0.0,disp);
}

// ---- synced lyrics (Caelestia's LyricList): fetched from lrclib.net (free, keyless, no account) ----
struct LyricLine{ double t; std::string text; };
static std::vector<LyricLine> g_lyrics; static std::mutex g_lyricsMtx;
static std::string g_lyricsKey;                        // title|artist we last fetched (draw-thread owned)
static std::atomic<int> g_lyricsState{0};              // 0 idle, 1 loading, 2 have synced, 3 none found, 4 plain only
static std::string HttpGet(const wchar_t* host,const wchar_t* path,bool https);   // fwd (defined below)
static std::string UrlEncodeQ(const std::string& s){   // percent-encode a query-string value
    static const char* hex="0123456789ABCDEF"; std::string o;
    for(unsigned char c:s){ if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')o+=(char)c;
        else if(c==' ')o+='+'; else { o+='%'; o+=hex[c>>4]; o+=hex[c&15]; } }
    return o;
}
static void ParseLrc(const std::string& lrc,std::vector<LyricLine>& out){
    size_t i=0,n=lrc.size();
    while(i<n){ size_t e=lrc.find('\n',i); std::string line=lrc.substr(i,(e==std::string::npos?n:e)-i);
        i=(e==std::string::npos)?n:e+1;
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::vector<double> stamps; size_t p=0;
        while(p<line.size()&&line[p]=='['){ size_t c=line.find(']',p); if(c==std::string::npos)break;
            std::string ts=line.substr(p+1,c-p-1);        // mm:ss.xx  (or mm:ss)
            int mm=0; double ss=0; if(sscanf(ts.c_str(),"%d:%lf",&mm,&ss)==2) stamps.push_back(mm*60.0+ss);
            p=c+1; }
        std::string text=line.substr(p); while(!text.empty()&&text[0]==' ')text.erase(text.begin());
        for(double t:stamps) if(t>=0) out.push_back({t,text}); }
    std::sort(out.begin(),out.end(),[](const LyricLine&a,const LyricLine&b){return a.t<b.t;});
}
// Settings (media.*)
static bool  g_mediaLyrics=true;                       // Media tab shows the lyrics column
static bool  g_lyricsToast=true;                       // the Now Playing toast sings along too
static float g_lyricsOffset=0.0f;                      // seconds added to the playhead for lyric timing (every song)
// The +/- buttons nudge THIS song only: timing errors come from one song's lyric file or one release of it,
// so a global nudge fixed that song and put every other song out by the same amount. Kept in
// %LOCALAPPDATA%\Aether\lyrics\offsets.txt as "artist|title<TAB>seconds".
static std::unordered_map<std::string,float> g_lyrSongOff; static bool g_lyrSongOffLoaded=false;
static std::wstring LyrOffsetsPath(){
    wchar_t base[MAX_PATH]; DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,MAX_PATH);
    if(!n||n>=MAX_PATH) return L"";
    std::wstring d=std::wstring(base)+L"\\Aether"; CreateDirectoryW(d.c_str(),nullptr);
    d+=L"\\lyrics"; CreateDirectoryW(d.c_str(),nullptr); return d+L"\\offsets.txt";
}
static std::string LyrSongKey(){ return g_md.artist+"|"+g_md.title; }
static float LyrSongOffset(){
    if(!g_lyrSongOffLoaded){ g_lyrSongOffLoaded=true;
        std::wstring p=LyrOffsetsPath(); FILE* f=p.empty()? nullptr : _wfopen(p.c_str(),L"rb");
        if(f){ char line[1024]; while(fgets(line,sizeof(line),f)){ std::string l(line); size_t t=l.rfind('\t');
                if(t!=std::string::npos) g_lyrSongOff[l.substr(0,t)]=(float)atof(l.c_str()+t+1); } fclose(f); } }
    auto it=g_lyrSongOff.find(LyrSongKey()); return it==g_lyrSongOff.end()? 0.0f : it->second;
}
static void LyrSongOffsetAdd(float d){
    float v=std::clamp(LyrSongOffset()+d,-10.0f,10.0f); if(fabsf(v)<0.001f) v=0.0f;
    if(v==0.0f) g_lyrSongOff.erase(LyrSongKey()); else g_lyrSongOff[LyrSongKey()]=v;
    std::wstring p=LyrOffsetsPath(); FILE* f=p.empty()? nullptr : _wfopen(p.c_str(),L"wb");
    if(f){ for(auto& kv:g_lyrSongOff) fprintf(f,"%s\t%.2f\n",kv.first.c_str(),kv.second); fclose(f); }
}
static double LyrOffset(){ return (double)g_lyricsOffset + (double)LyrSongOffset(); }
static std::vector<std::string> g_lyricsPlain;         // unsynced lyrics, when that is all there is
static std::atomic<int> g_lyricsGen{0};                // bumps per track, so a slow reply for the last song is dropped
// A JSON string value at a key, or false for null/missing. Handles \uXXXX (lyrics are full of it) and
// never runs into the NEXT field the way jstr does on "syncedLyrics": null.
static bool JsonStrAt(const std::string& s,size_t from,std::string& out){
    size_t c=s.find(':',from); if(c==std::string::npos) return false;
    size_t q=c+1; while(q<s.size()&&(s[q]==' '||s[q]=='\t'||s[q]=='\r'||s[q]=='\n')) q++;
    if(q>=s.size()||s[q]!='"') return false;
    out.clear();
    auto put=[&](unsigned cp){
        if(cp<0x80) out+=(char)cp;
        else if(cp<0x800){ out+=(char)(0xC0|(cp>>6)); out+=(char)(0x80|(cp&0x3F)); }
        else if(cp<0x10000){ out+=(char)(0xE0|(cp>>12)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); }
        else { out+=(char)(0xF0|(cp>>18)); out+=(char)(0x80|((cp>>12)&0x3F)); out+=(char)(0x80|((cp>>6)&0x3F)); out+=(char)(0x80|(cp&0x3F)); } };
    for(size_t i=q+1;i<s.size();i++){
        char ch=s[i];
        if(ch=='"') return true;
        if(ch=='\\' && i+1<s.size()){
            char e=s[++i];
            if(e=='n') out+='\n'; else if(e=='t') out+='\t'; else if(e=='r') out+='\r';
            else if(e=='u' && i+4<s.size()){
                unsigned cp=(unsigned)strtoul(s.substr(i+1,4).c_str(),nullptr,16); i+=4;
                if(cp>=0xD800 && cp<0xDC00 && i+6<s.size() && s[i+1]=='\\' && s[i+2]=='u'){
                    unsigned lo=(unsigned)strtoul(s.substr(i+3,4).c_str(),nullptr,16);
                    if(lo>=0xDC00 && lo<0xE000){ cp=0x10000+((cp-0xD800)<<10)+(lo-0xDC00); i+=6; } }
                put(cp);
            } else out+=e;
        } else out+=ch;
    }
    return true;
}
// "Song (feat. X) - 2011 Remaster [Official Video]" -> "Song": what lrclib actually has a record for
static std::string LyrCleanTitle(std::string t){
    auto low=[](std::string x){ for(auto& c:x) c=(char)tolower((unsigned char)c); return x; };
    for(int pass=0;pass<4;pass++){
        bool cut=false;
        for(const char* br : {"()","[]"}){
            size_t o=t.find(br[0]); if(o==std::string::npos) continue;
            size_t cl=t.find(br[1],o); if(cl==std::string::npos) continue;
            std::string in=low(t.substr(o,cl-o));
            for(const char* w : {"feat","ft.","remaster","official","video","lyric","audio","prod","visualizer","mv"})
                if(in.find(w)!=std::string::npos){ t.erase(o,cl-o+1); cut=true; break; }
            if(cut) break;
        }
        if(!cut) break;
    }
    { std::string l=low(t); size_t d=l.find(" - ");
      if(d!=std::string::npos){ std::string tail=l.substr(d);
          for(const char* w : {"remaster","version","mono","stereo","edit","live","mix"})
              if(tail.find(w)!=std::string::npos){ t=t.substr(0,d); break; } } }
    while(!t.empty()&&t.back()==' ') t.pop_back();
    while(!t.empty()&&t.front()==' ') t.erase(t.begin());
    return t;
}
static std::atomic<int> g_lyricsSrc{0};                // 1 lrclib, 2 NetEase (shown in the Media tab header)
// NetEase Cloud Music's public web API has TIMED lyrics for many songs lrclib only knows as plain text
// ("Unlike Me" - Kate Havnevik: plain on lrclib, fully timed here). Search, pick the entry whose title,
// artist and length match the playing track, then take its LRC. Plain GETs, no account, no key.
static std::string LyrNorm(const std::string& x){ std::string o; for(unsigned char c:x){ if(isalnum(c)) o+=(char)tolower(c); else if(c>=0x80) o+=(char)c; } return o; }
static bool NeteaseLyrics(const std::string& title,const std::string& artist,double dur,std::vector<LyricLine>& out){
    std::string ct=LyrCleanTitle(title);
    std::string resp=HttpGet(L"music.163.com",U82W("/api/cloudsearch/pc?type=1&limit=10&s="+UrlEncodeQ(ct+" "+artist)).c_str(),true);
    size_t sp=resp.find("\"songs\":["); if(sp==std::string::npos) return false;
    // split the songs array into its top-level objects
    std::vector<std::string> songs; { int depth=0; bool str=false; size_t st=0;
      for(size_t i=sp+8;i<resp.size();i++){ char c=resp[i];
          if(str){ if(c=='\\') i++; else if(c=='"') str=false; continue; }
          if(c=='"') str=true;
          else if(c=='['||c=='{'){ if(depth==1 && c=='{') st=i; depth++; }
          else if(c==']'||c=='}'){ depth--; if(depth==1 && c=='}') songs.push_back(resp.substr(st,i-st+1)); if(depth==0) break; } } }
    std::string nt=LyrNorm(ct), na=LyrNorm(artist);
    long long bestId=0; int bestScore=0;
    for(auto& o:songs){
        std::string name; size_t kp=jkey(o,"name"); if(kp==std::string::npos || !JsonStrAt(o,kp,name)) continue;
        double id=jnum(o,"id"); if(std::isnan(id)) continue;
        double dt=jnum(o,"dt");
        std::string nn=LyrNorm(LyrCleanTitle(name));
        bool titleOk = !nn.empty() && !nt.empty() && (nn==nt || nn.find(nt)!=std::string::npos || nt.find(nn)!=std::string::npos);
        if(!titleOk) continue;
        bool artistOk=false;
        { size_t ar=o.find("\"ar\":["); size_t arEnd= ar==std::string::npos? ar : o.find(']',ar);
          if(ar!=std::string::npos){ std::string arr=o.substr(ar,arEnd-ar); size_t k=0;
              while((k=jkey(arr,"name",k))!=std::string::npos){ std::string an; if(JsonStrAt(arr,k,an)){ std::string x=LyrNorm(an);
                      if(!x.empty() && !na.empty() && (na.find(x)!=std::string::npos || x.find(na)!=std::string::npos)) artistOk=true; }
                  k+=6; } } }
        double diff = (dur>1 && !std::isnan(dt) && dt>0)? fabs(dt/1000.0-dur) : 99.0;
        int score = (artistOk?4:0) + (nn==nt?2:1) + (diff<=3.0?3:(diff<=8.0?1:0));
        if(!artistOk && diff>3.0) continue;                        // a same-named song by someone else
        if(dur>1 && diff>15.0) continue;                           // a different edit / live version
        if(score>bestScore){ bestScore=score; bestId=(long long)id; }
    }
    if(!bestId) return false;
    std::string lr=HttpGet(L"music.163.com",U82W("/api/song/lyric?lv=1&id="+std::to_string(bestId)).c_str(),true);
    size_t kp=jkey(lr,"lyric"); std::string lrc; if(kp==std::string::npos || !JsonStrAt(lr,kp,lrc)) return false;
    std::vector<LyricLine> tmp; ParseLrc(lrc,tmp);
    // credits NetEase prefixes as lyric lines ("作词 : ...", "Composer : ..."), and its instrumental marker
    for(auto& L:tmp){
        const std::string& t=L.text;
        if(t.find(" : ")!=std::string::npos && t.size()<80 && L.t<30.0) continue;
        if(t.find("\xE7\xBA\xAF\xE9\x9F\xB3\xE4\xB9\x90")!=std::string::npos) { tmp.clear(); break; }   // 纯音乐 (instrumental)
        out.push_back(L); }
    int words=0; for(auto& L:out) if(!L.text.empty()) words++;
    if(words<3){ out.clear(); return false; }
    return true;
}
static std::wstring LyricsCachePath(const std::string& key){
    wchar_t base[MAX_PATH]; DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,MAX_PATH);
    if(!n||n>=MAX_PATH) return L"";
    std::wstring dir=std::wstring(base)+L"\\Aether"; CreateDirectoryW(dir.c_str(),nullptr);
    dir+=L"\\lyrics"; CreateDirectoryW(dir.c_str(),nullptr);
    uint64_t h=1469598103934665603ULL; for(unsigned char c:key){ h^=c; h*=1099511628211ULL; }
    wchar_t nm[32]; swprintf(nm,32,L"\\%016llx.lrc",(unsigned long long)h);
    return dir+nm;
}
static void FetchLyricsAsync(std::string title,std::string artist,std::string album,double dur){
    g_lyricsState.store(1);
    int gen=++g_lyricsGen;
    std::thread([=]{
        std::vector<LyricLine> synced; std::vector<std::string> plain; bool answered=false;
        std::string key=artist+"|"+title;
        std::wstring cache=LyricsCachePath(key);
        auto splitPlain=[&](const std::string& p){ plain.clear(); size_t i=0;
            while(i<=p.size()){ size_t e=p.find('\n',i); std::string ln=p.substr(i,(e==std::string::npos?p.size():e)-i);
                if(!ln.empty()&&ln.back()=='\r') ln.pop_back(); plain.push_back(ln); if(e==std::string::npos) break; i=e+1; } };
        // ---- cache ----
        bool fromCache=false;
        std::vector<LyricLine> stale; int staleSrc=0;          // pre-"#v2" lyrics: re-checked, kept if the check cannot run
        if(!cache.empty()){
            FILE* f=_wfopen(cache.c_str(),L"rb");
            if(f){ std::string c; char buf[4096]; size_t r; while((r=fread(buf,1,sizeof(buf),f))>0) c.append(buf,r); fclose(f);
                if(c.rfind("#none2",0)==0){ fromCache=true; }             // "#none" (no NetEase try yet) is retried
                else if(c.rfind("#plain2\n",0)==0){ splitPlain(c.substr(8)); fromCache=!plain.empty(); }
                else if(c.rfind("#none",0)==0 || c.rfind("#plain",0)==0){ fromCache=false; }
                else {
                    // Entries from before the title/artist check (no "#v2") may be another song's lyrics - the
                    // lookup used to accept the first timed result of a fuzzy search. Look them up again once.
                    bool v2=c.rfind("#v2\n",0)==0; if(v2) c=c.substr(4);
                    int src=1; if(c.rfind("#ne\n",0)==0){ src=2; c=c.substr(4); }
                    if(v2){ g_lyricsSrc.store(src); ParseLrc(c,synced); fromCache=!synced.empty(); }
                    else { ParseLrc(c,stale); staleSrc=src; } } }
        }
        if(!fromCache){
            auto tryResp=[&](const std::string& resp,double wantDur)->bool{
                if(resp.empty()) return false; answered=true;
                // a /get reply is one object; a /search reply is an array of them - walk every "id"
                size_t pos=0; double bestD=1e9; int bestScore=-1; std::string bestSync, bestPlain;
                std::vector<size_t> starts; { size_t k=0; while((k=resp.find("\"id\"",k))!=std::string::npos){ starts.push_back(k); k+=4; } }
                if(starts.empty()) starts.push_back(0);
                for(size_t si=0;si<starts.size();si++){
                    size_t a=starts[si], b=(si+1<starts.size())? starts[si+1] : resp.size();
                    std::string obj=resp.substr(a,b-a);
                    double d=jnum(obj,"duration"); double diff=(wantDur>1 && !std::isnan(d))? fabs(d-wantDur) : 0.0;
                    if(wantDur>1 && !std::isnan(d) && diff>8.0) continue;          // a different recording
                    int matchScore=0;
                    // Is it even this song? A fuzzy search happily returns another artist's song of the same name,
                    // and with no length to compare (browsers often report none) that one was taken. Title must
                    // match; the artist must match too unless the length agrees to within 2 s.
                    { std::string tn, an; size_t k2;
                      if((k2=jkey(obj,"trackName"))!=std::string::npos) JsonStrAt(obj,k2,tn);
                      if((k2=jkey(obj,"artistName"))!=std::string::npos) JsonStrAt(obj,k2,an);
                      std::string wt=LyrNorm(LyrCleanTitle(title)), gt=LyrNorm(LyrCleanTitle(tn));
                      std::string wa=LyrNorm(artist), ga=LyrNorm(an);
                      if(wt.empty() && title.find(" - ")!=std::string::npos) wt=LyrNorm(LyrCleanTitle(title.substr(title.find(" - ")+3)));
                      bool titleOk = gt.empty() || wt.empty() || gt==wt || gt.find(wt)!=std::string::npos || wt.find(gt)!=std::string::npos
                                     || (title.find(" - ")!=std::string::npos && LyrNorm(title).find(gt)!=std::string::npos);
                      // no artist from the player (a browser video): the result's artist has to be named in the title
                      bool artistOk = !ga.empty() && ( (!wa.empty() && (ga.find(wa)!=std::string::npos || wa.find(ga)!=std::string::npos))
                                                       || LyrNorm(title).find(ga)!=std::string::npos );
                      bool lengthOk = wantDur>1 && !std::isnan(d) && diff<=2.0;
                      if(!titleOk) continue;
                      if(!artistOk && !lengthOk) continue;
                      // rank: an exact title beats a longer one containing it ("Hello" over "Hello Hello Hello"),
                      // a matching artist beats a matching length, then the closer length wins
                      matchScore = (gt==wt? 4 : 0) + (artistOk? 2 : 0) + (lengthOk? 1 : 0); }
                    std::string sy, pl; size_t kp;
                    if((kp=jkey(obj,"syncedLyrics"))!=std::string::npos) JsonStrAt(obj,kp,sy);
                    if((kp=jkey(obj,"plainLyrics"))!=std::string::npos)  JsonStrAt(obj,kp,pl);
                    if(!sy.empty() && (matchScore>bestScore || (matchScore==bestScore && diff<bestD))){ bestScore=matchScore; bestD=diff; bestSync=sy; }
                    if(bestPlain.empty() && !pl.empty()) bestPlain=pl;
                    (void)pos;
                }
                if(!bestSync.empty()){ synced.clear(); ParseLrc(bestSync,synced); if(!synced.empty()) return true; }
                if(!bestPlain.empty() && plain.empty()) splitPlain(bestPlain);
                return false; };
            std::string ct=LyrCleanTitle(title);
            char dd[32]={0}; if(dur>1) snprintf(dd,32,"&duration=%d",(int)(dur+0.5));
            bool ok=false;
            if(!artist.empty()){
                std::string p="/api/get?artist_name="+UrlEncodeQ(artist)+"&track_name="+UrlEncodeQ(ct);
                if(!album.empty()) ok=tryResp(HttpGet(L"lrclib.net",U82W(p+"&album_name="+UrlEncodeQ(album)+dd).c_str(),true),dur);
                if(!ok) ok=tryResp(HttpGet(L"lrclib.net",U82W(p+dd).c_str(),true),dur);
                if(!ok) ok=tryResp(HttpGet(L"lrclib.net",U82W("/api/search?artist_name="+UrlEncodeQ(artist)+"&track_name="+UrlEncodeQ(ct)).c_str(),true),dur);
            }
            // a video title carries the artist itself: "Artist - Song (Official Video)"
            if(!ok){ size_t d=title.find(" - ");
                std::string q = (d!=std::string::npos)? LyrCleanTitle(title.substr(0,d))+" "+LyrCleanTitle(title.substr(d+3))
                                                     : ct+" "+artist;
                ok=tryResp(HttpGet(L"lrclib.net",U82W("/api/search?q="+UrlEncodeQ(q)).c_str(),true),dur); }
            bool fromNe=false;
            if(ok) g_lyricsSrc.store(1);
            else {                                                    // no timed lyrics on lrclib: ask NetEase
                std::vector<LyricLine> ne; std::string a2=artist, t2=title;
                if(a2.empty()){ size_t d=title.find(" - "); if(d!=std::string::npos){ a2=title.substr(0,d); t2=title.substr(d+3); } }
                if(NeteaseLyrics(t2,a2,dur,ne)){ synced=std::move(ne); ok=true; fromNe=true; answered=true; g_lyricsSrc.store(2); }
            }
            if(!cache.empty() && answered){
                FILE* f=_wfopen(cache.c_str(),L"wb");
                if(f){
                    if(!synced.empty()){ fputs("#v2\n",f); if(fromNe) fputs("#ne\n",f);
                        for(auto& L:synced){ int mm=(int)(L.t/60); double ss=L.t-mm*60; fprintf(f,"[%02d:%05.2f]%s\n",mm,ss,L.text.c_str()); } }
                    else if(!plain.empty()){ fputs("#plain2\n",f); for(auto& l:plain){ fputs(l.c_str(),f); fputc('\n',f); } }
                    else fputs("#none2\n",f);
                    fclose(f); }
            }
        }
        if(synced.empty() && !answered && !stale.empty()){ synced=std::move(stale); g_lyricsSrc.store(staleSrc); }   // offline: keep what we had
        if(gen!=g_lyricsGen.load()) return;                           // the song changed while we were asking
        { std::lock_guard<std::mutex> lk(g_lyricsMtx); g_lyrics=std::move(synced); g_lyricsPlain=std::move(plain); }
        std::lock_guard<std::mutex> lk(g_lyricsMtx);
        g_lyricsState.store(!g_lyrics.empty()? 2 : (!g_lyricsPlain.empty()? 4 : 3));
    }).detach();
}
// Called from the draw thread each frame; kicks off a fetch when the track changes.
// A player hands over the new TITLE before it refreshes the song's LENGTH (Spotify updates its timeline on its
// own schedule), and the lookup used the length to pick the right recording - so right after a skip it filtered
// by the PREVIOUS song's length and could take a different version, or turn the right one down, and cache that.
// The fetch now waits until the length has changed too (or 2.5 s, for two songs of the same length).
static void LyricsMaybeFetch(){
    static ULONGLONG changedAt=0; static double durAtChange=0; static bool pending=false;
    bool has=g_md.has&&!g_md.title.empty();
    std::string key = has? (g_md.title+"|"+g_md.artist) : "";
    const ULONGLONG now=GetTickCount64();
    if(key!=g_lyricsKey){ g_lyricsKey=key;
        { std::lock_guard<std::mutex> lk(g_lyricsMtx); g_lyrics.clear(); g_lyricsPlain.clear(); }
        g_lyricsState.store(has? 1 : 0); ++g_lyricsGen;
        changedAt=now; durAtChange=g_md.dur; pending=has; }
    if(pending && has){
        const ULONGLONG since=now-changedAt;
        const bool lengthMoved = g_md.dur>0.0 && fabs(g_md.dur-durAtChange)>0.5;
        if((since>=350 && lengthMoved) || since>=2500 || (since>=350 && durAtChange<=0.0 && g_md.dur>0.0)){
            pending=false; FetchLyricsAsync(g_md.title,g_md.artist,g_md.album,g_md.dur); } }
}
static ID3D11ShaderResourceView* g_mdArt=nullptr;      // album cover texture (published by the media thread)
static std::atomic<ULONGLONG> g_medUntil{0}; static HWND g_medWake=nullptr;   // "Now Playing" flyout trigger
// Every texture the shell creates is made here, so every one can be accounted for. Testers saw the
// process climb from ~200 MB to 700 MB and 1 GB and then crash "after a while"; the call site of each
// live texture (function + line) is recorded so `Aether.exe -s texdump` can say which one never dies.
static ID3D11ShaderResourceView* MakeTextureBGRA_(const void* bits,int w,int h,const char* fn,int line);
#define MakeTextureBGRA(b,w,h) MakeTextureBGRA_((b),(w),(h),__FUNCTION__,__LINE__)
static std::atomic<int>  g_reqPlay{0}, g_reqNext{0}, g_reqPrev{0};
static std::atomic<int>  g_reqShuffle{0}, g_reqRepeat{0};
static std::atomic<double> g_reqSeek{-1.0};        // seconds to seek to, -1 = nothing pending
static std::atomic<unsigned> g_mdTint{0};          // dominant colour of the cover, 0 = unknown
static std::atomic<int>  g_mdShuffle{-1};          // -1 unknown, 0 off, 1 on
static std::atomic<int>  g_mdRepeat{-1};           // -1 unknown, 0 none, 1 track, 2 list
static std::string g_mdSource;                     // the app the audio is coming from
static std::string g_mdSourceId;                   // ...and its app id (exe name or AUMID), to bring it forward
// Every player that currently has a media session, so the source row's dropdown can switch between
// them. SMTC has no QUEUE api of any kind - a player's upcoming-tracks list is simply not exposed -
// so "the list behind the chevron" is the player picker, which is what it is in the reference too.
struct MediaSession{ std::string id, name; bool playing=false; };
static std::vector<MediaSession> g_mdSessions; static std::mutex g_mdSessMtx;
static std::string g_mdPick;                       // "" = follow whatever Windows calls current
static std::mutex g_mdPickMtx;
// SourceAppUserModelId is an AUMID like "Spotify.exe!Spotify" or a packaged-app hash - trim it to
// something a person recognises. Factored out so the picker list and the pill agree.
static std::string PrettySource(std::string src){
    size_t bang=src.find('!');  if(bang!=std::string::npos) src=src.substr(0,bang);
    size_t bs=src.find_last_of("\\/"); if(bs!=std::string::npos) src=src.substr(bs+1);
    size_t us=src.find('_');    if(us!=std::string::npos) src=src.substr(0,us);
    size_t dot=src.find_last_of('.');
    if(dot!=std::string::npos && src.size()-dot<=5) src=src.substr(0,dot);
    { size_t sp=src.find_last_of(". ");
      if(sp!=std::string::npos && src.size()-sp>10) src=src.substr(0,sp);
      bool shouty=true; for(char ch:src) if(islower((unsigned char)ch)) { shouty=false; break; }
      if(shouty && src.size()>12) src.clear(); }
    return src;
}

// the cover's dominant colour, so the whole tab can take on the mood of what is playing
static unsigned VibrantFromBuffer(const uint8_t* data,size_t len){
    IStream* st=SHCreateMemStream(data,(UINT)len); if(!st) return 0;
    unsigned out=0; IWICImagingFactory* fac=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))){
        IWICBitmapDecoder* dec=nullptr;
        if(SUCCEEDED(fac->CreateDecoderFromStream(st,nullptr,WICDecodeMetadataCacheOnDemand,&dec))){
            IWICBitmapFrameDecode* fr=nullptr;
            if(SUCCEEDED(dec->GetFrame(0,&fr))){
                IWICBitmapScaler* sc=nullptr; IWICFormatConverter* cv=nullptr;
                if(SUCCEEDED(fac->CreateBitmapScaler(&sc)) && SUCCEEDED(sc->Initialize(fr,48,48,WICBitmapInterpolationModeFant)) &&
                   SUCCEEDED(fac->CreateFormatConverter(&cv)) &&
                   SUCCEEDED(cv->Initialize(sc,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                    std::vector<uint8_t> px(48*48*4);
                    if(SUCCEEDED(cv->CopyPixels(nullptr,48*4,(UINT)px.size(),px.data()))){
                        double ar=0,ag=0,ab=0,w=0;
                        for(size_t i=0;i+3<px.size();i+=4){
                            double b=px[i],g=px[i+1],r=px[i+2];
                            double mx=std::max(std::max(r,g),b), mn=std::min(std::min(r,g),b);
                            double v=mx/255.0, sat=mx>0?(mx-mn)/mx:0;
                            double wt=sat*sat*v;                 // favour colourful, bright pixels
                            ar+=r*wt; ag+=g*wt; ab+=b*wt; w+=wt;
                        }
                        if(w>1e-6){
                            int r=(int)std::clamp(ar/w,0.0,255.0), g=(int)std::clamp(ag/w,0.0,255.0), b=(int)std::clamp(ab/w,0.0,255.0);
                            // keep it readable: lift very dark results
                            int mx2=std::max(std::max(r,g),b);
                            if(mx2<90 && mx2>0){ double k=90.0/mx2; r=(int)std::min(255.0,r*k); g=(int)std::min(255.0,g*k); b=(int)std::min(255.0,b*k); }
                            out=IM_COL32(r,g,b,255);
                        }
                    }
                }
                if(cv)cv->Release(); if(sc)sc->Release(); fr->Release();
            }
            dec->Release();
        }
        fac->Release();
    }
    st->Release();
    return out;
}
// decode an image byte-buffer (album art) to a DX texture via WIC
static int g_mdArtW=0,g_mdArtH=0;      // the cover's real size, so it is never stretched
static ID3D11ShaderResourceView* DecodeBufferToTexture(const uint8_t* data,size_t len){
    IStream* st=SHCreateMemStream(data,(UINT)len); if(!st)return nullptr;
    ID3D11ShaderResourceView* out=nullptr; IWICImagingFactory* fac=nullptr;
    if(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fac)))){
        IWICBitmapDecoder* dec=nullptr;
        if(SUCCEEDED(fac->CreateDecoderFromStream(st,nullptr,WICDecodeMetadataCacheOnLoad,&dec))){
            IWICBitmapFrameDecode* fr=nullptr;
            if(SUCCEEDED(dec->GetFrame(0,&fr))){
                IWICFormatConverter* cv=nullptr; fac->CreateFormatConverter(&cv);
                if(cv && SUCCEEDED(cv->Initialize(fr,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeMedianCut))){
                    UINT w=0,h=0; cv->GetSize(&w,&h);
                    if(w&&h){ std::vector<uint8_t> buf((size_t)w*h*4);
                        if(SUCCEEDED(cv->CopyPixels(nullptr,w*4,(UINT)buf.size(),buf.data()))){
                            out=MakeTextureBGRA(buf.data(),w,h);
                            if(out){ g_mdArtW=(int)w; g_mdArtH=(int)h; }   // keep the aspect for drawing
                        } }
                }
                if(cv)cv->Release(); fr->Release();
            }
            dec->Release();
        }
        fac->Release();
    }
    st->Release(); return out;
}

static bool g_mediaFaked=false;   // --testmedia: hold the staged track, do not poll over it
static void MediaThread(){
    using namespace winrt::Windows::Media::Control;
    using namespace winrt::Windows::Storage::Streams;
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch(...){}
    GlobalSystemMediaTransportControlsSessionManager mgr{nullptr};
    try { mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get(); } catch(...) { return; }
    std::string lastArtKey;
    // The "Now Playing" toast must fire on a REAL track change only. Players (Discord, browsers,
    // Spotify) routinely drop and re-create their session, and metadata lands in stages, which made
    // lastArtKey churn and re-popped the toast for the same song over and over. lastPopKey survives
    // a session dropping out, and a cooldown stops any residual flapping.
    std::string lastPopKey; ULONGLONG lastPopAt=0;
    while (g_running) {
        // --testmedia stages a fake track so the Media tab can be screenshotted with no player
        // running. The poll below would overwrite it on its first pass, so it stands down entirely.
        if(g_mediaFaked){ Sleep(400); continue; }
        try {
            // publish every live session, then act on the one the user picked (if it is still there)
            GlobalSystemMediaTransportControlsSession s{nullptr};
            try {
                auto all = mgr.GetSessions();
                std::vector<MediaSession> list;
                std::string want; { std::lock_guard<std::mutex> lk(g_mdPickMtx); want=g_mdPick; }
                for (uint32_t i=0;i<all.Size();i++){
                    auto cand = all.GetAt(i);
                    std::string id = winrt::to_string(cand.SourceAppUserModelId());
                    MediaSession ms; ms.id=id; ms.name=PrettySource(id);
                    if(ms.name.empty()) ms.name="Player";
                    try { ms.playing = cand.GetPlaybackInfo().PlaybackStatus()==
                                       GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing; } catch(...){}
                    list.push_back(ms);
                    if(!want.empty() && id==want) s=cand;
                }
                { std::lock_guard<std::mutex> lk(g_mdSessMtx); g_mdSessions=list; }
                // the picked player closed -> fall back to following the system, so the tab never
                // strands itself on a session that no longer exists
                if(!want.empty() && !s){ std::lock_guard<std::mutex> lk(g_mdPickMtx); g_mdPick.clear(); }
            } catch(...){}
            if (!s) s = mgr.GetCurrentSession();
            if (s) {
                if (g_reqPlay.exchange(0)) s.TryTogglePlayPauseAsync();
                if (g_reqNext.exchange(0)) s.TrySkipNextAsync();
                if (g_reqPrev.exchange(0)) s.TrySkipPreviousAsync();
                { double sk=g_reqSeek.exchange(-1.0);
                  if(sk>=0) try { s.TryChangePlaybackPositionAsync((int64_t)(sk*10000000.0)); } catch(...){} }
                if (g_reqShuffle.exchange(0)) try {
                    auto cur=s.GetPlaybackInfo().IsShuffleActive();
                    bool on = cur? cur.Value() : false;
                    s.TryChangeShuffleActiveAsync(!on);
                } catch(...){}
                if (g_reqRepeat.exchange(0)) try {
                    using RM = winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
                    auto cur=s.GetPlaybackInfo().AutoRepeatMode();
                    RM next = RM::None;
                    if(cur){ RM v=cur.Value(); next = (v==RM::None)? RM::List : (v==RM::List)? RM::Track : RM::None; }
                    else next = RM::List;
                    s.TryChangeAutoRepeatModeAsync(next);
                } catch(...){}
                auto props = s.TryGetMediaPropertiesAsync().get();
                Media m; m.has=true;
                m.title  = winrt::to_string(props.Title());
                m.artist = winrt::to_string(props.Artist());
                m.album  = winrt::to_string(props.AlbumTitle());
                auto pi = s.GetPlaybackInfo();
                m.playing = pi.PlaybackStatus()==GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                try { auto sh=pi.IsShuffleActive(); g_mdShuffle = sh? (sh.Value()?1:0) : -1; } catch(...){ g_mdShuffle=-1; }
                try { using RM = winrt::Windows::Media::MediaPlaybackAutoRepeatMode;
                      auto rp=pi.AutoRepeatMode();
                      g_mdRepeat = rp? ((rp.Value()==RM::None)?0:(rp.Value()==RM::Track)?1:2) : -1;
                } catch(...){ g_mdRepeat=-1; }
                try { g_mdSource=PrettySource(winrt::to_string(s.SourceAppUserModelId())); } catch(...){}
                auto tl = s.GetTimelineProperties();
                // sub-second, and moved on by however long ago the player took the reading: Spotify only
                // refreshes its timeline on play/pause/seek, so the raw Position can be minutes stale
                double p = std::chrono::duration<double>(tl.Position()).count();
                double e = std::chrono::duration<double>(tl.EndTime()).count();
                { static bool wasPlaying=false, seenOnce=false; static winrt::clock::time_point playSince{};
                  auto nowc=winrt::clock::now();
                  if(m.playing && !wasPlaying && seenOnce) playSince=nowc;          // resumed while we were watching
                  if(!seenOnce){ seenOnce=true; if(m.playing) playSince=winrt::clock::time_point{}; }
                  wasPlaying=m.playing;
                  if(m.playing){
                      try { double ago=std::chrono::duration<double>(nowc-tl.LastUpdatedTime()).count();
                            // right after a resume the player can still hold the reading it took when it PAUSED;
                            // counting all the paused time as playing threw the lyrics seconds ahead
                            if(playSince!=winrt::clock::time_point{}){
                                double since=std::chrono::duration<double>(nowc-playSince).count();
                                if(since>=0.0) ago=std::min(ago,since); }
                            if(ago>0.0 && ago<6*3600.0) p+=ago; } catch(...){}
                  } }
                if(e>0.0) p=std::min(p,e);
                m.pos = std::max(0.0,p); m.dur = e; m.posTick = GetTickCount64();
                try { g_mdSourceId=winrt::to_string(s.SourceAppUserModelId()); } catch(...){}
                g_md = m;
                // album cover art (only re-decode when the track changes)
                std::string key=m.title+"|"+m.artist;
                if (key!=lastArtKey) { lastArtKey=key;
                    ID3D11ShaderResourceView* art=nullptr;
                    try { auto tref=props.Thumbnail();
                        if (tref) { auto stream=tref.OpenReadAsync().get(); uint64_t sz=stream.Size();
                            if (sz>0 && sz<20000000){ DataReader dr(stream); dr.LoadAsync((uint32_t)sz).get();
                                std::vector<uint8_t> buf((size_t)sz); dr.ReadBytes(winrt::array_view<uint8_t>(buf.data(),buf.data()+buf.size()));
                                art=DecodeBufferToTexture(buf.data(),buf.size());
                                g_mdTint = VibrantFromBuffer(buf.data(),buf.size()); } } } catch(...){}
                    if(!g_mdArt && !art) g_mdTint=0;
                    g_mdArt=art;   // publish (old one intentionally leaked to avoid a use-after-free race; rare)
                    // new track started -> pop the Medal-style flyout. Guarded so the SAME song can't
                    // re-pop when a player briefly drops its session or fills metadata in late.
                    // Two guards: the same song can't re-pop for 30s (covers a player dropping and
                    // re-creating its session), and NOTHING can pop within 3s of the last toast
                    // (covers two players flapping for the "current session", which alternates the
                    // key every 600ms poll and used to fire the toast continuously).
                    ULONGLONG nowt=GetTickCount64();
                    if (m.playing && !m.title.empty() &&
                        nowt-lastPopAt>3000 &&
                        (key!=lastPopKey || nowt-lastPopAt>30000)){
                        lastPopKey=key; lastPopAt=nowt;
                        g_medUntil = nowt+4500; if(g_medWake) PostMessageW(g_medWake,WM_NULL,0,0); }
                }
            } else { g_md.has=false; g_mdArt=nullptr; lastArtKey.clear();     // lastPopKey deliberately kept
                     std::lock_guard<std::mutex> lk(g_mdSessMtx); g_mdSessions.clear(); }
        } catch(...) {}
        Sleep(600);
    }
}

// ---- notifications (WinRT UserNotificationListener; needs the MSIX package identity) ----
// `at` = when WE first saw this notification, so the card can show a relative age. The listener
// hands back the whole notification-centre backlog on every poll with no timestamps of its own,
// so first-seen is tracked here and carried across polls in g_notifFirst.
struct Notif { uint32_t id=0; std::string app,title,body,aumid; ULONGLONG at=0; };
// Windows exposes NO way to invoke another app's toast buttons - UserNotificationListener can read
// a notification and remove it, nothing more. So "acting" on one means opening the app that sent
// it, which the AppUserModelId gives us. shell:AppsFolder\<aumid> launches packaged AND desktop
// apps without any COM plumbing, and focuses the running instance if there is one.
static void ActivateAumid(const std::string& aumid){
    if(aumid.empty()) return;
    std::wstring t=L"shell:AppsFolder\\"+U82W(aumid);
    AetherShellExec(nullptr,L"open",t.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
}
static std::vector<Notif> g_notifs; static std::mutex g_notifMtx;
static std::atomic<ULONGLONG> g_notifChanged{0};
static std::atomic<uint32_t> g_reqDismiss{0}; static std::atomic<int> g_reqClearAll{0};
// Every dismissal is queued. The single atomic above kept only the LAST id, so dismissing a group, or two
// cards quickly, silently lost all but one - and the lost ones came straight back on the next poll.
static std::mutex g_dismissMtx; static std::vector<uint32_t> g_dismissQ;
static void NotifDismiss(uint32_t id){ std::lock_guard<std::mutex> lk(g_dismissMtx); g_dismissQ.push_back(id); }
// app icons, by app user model id (nullptr = asked and there was none)
static std::mutex g_nIconMtx; static std::unordered_map<std::string,ID3D11ShaderResourceView*> g_nIcons;
// the listener thread only fetches the bytes; textures are made on the render thread (the device may not
// exist yet when the first poll runs - that was a null-pointer crash at startup)
static std::vector<std::pair<std::string,std::vector<uint8_t>>> g_nIconBytes;
static ID3D11ShaderResourceView* DecodeBufferToTexture(const uint8_t* data,size_t len);   // fwd
static bool g_notifAllowed=false; static HWND g_notifWake=nullptr; static bool g_forceNotif=false;
// how far down the stack currently reaches (published by DrawNotifications) — the hover keep-alive
// zone follows the real cards instead of claiming the whole right-hand column of the screen.
static float g_notifStackBot=0.0f;
// set by the bar's bell: hold the panel open for a few seconds, then let the normal hover
// keep-alive decide. A latch would strand it on screen; this behaves like every other reveal.
static ULONGLONG g_notifForceUntil=0;

static void NotifThread(){
    using namespace winrt::Windows::UI::Notifications;
    using namespace winrt::Windows::UI::Notifications::Management;
    try { winrt::init_apartment(winrt::apartment_type::multi_threaded); } catch(...){}
    UserNotificationListener listener{nullptr};
    try {
        listener = UserNotificationListener::Current();
        auto st = listener.RequestAccessAsync().get();
        if (st != UserNotificationListenerAccessStatus::Allowed) return;   // unpackaged / denied
    } catch(...) { return; }
    g_notifAllowed=true;
    // Pop the panel only for notifications we have never seen before. The old code popped whenever
    // the COUNT went up, which re-fired on any bounce, and treated the very first poll as "new" —
    // so every shell start threw the whole notification-centre backlog on screen.
    std::unordered_map<uint32_t,ULONGLONG> seen;   // id -> first seen
    bool firstPoll=true;
    while (g_running) {
        try {
            uint32_t did=g_reqDismiss.exchange(0); if(did) NotifDismiss(did);
            { std::vector<uint32_t> q; { std::lock_guard<std::mutex> lk(g_dismissMtx); q.swap(g_dismissQ); }
              for(uint32_t d:q) try { listener.RemoveNotification(d); } catch(...){} }
            if(g_reqClearAll.exchange(0)) listener.ClearNotifications();
            auto ns = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
            std::vector<Notif> list;
            std::unordered_map<uint32_t,ULONGLONG> fresh;
            ULONGLONG now=GetTickCount64();
            bool arrived=false;
            for (auto n : ns) {
                Notif nf; nf.id=n.Id();
                try { nf.app = winrt::to_string(n.AppInfo().DisplayInfo().DisplayName()); } catch(...){}
                try { nf.aumid = winrt::to_string(n.AppInfo().AppUserModelId()); } catch(...){}
                try { auto b=n.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
                    if(b){ auto tx=b.GetTextElements(); for(uint32_t i=0;i<tx.Size();i++){ std::string t=winrt::to_string(tx.GetAt(i).Text());
                        if(i==0)nf.title=t; else { if(!nf.body.empty())nf.body+="\n"; nf.body+=t; } } } } catch(...){}
                // the sending app's own icon, fetched once per app
                { std::string key = nf.aumid.empty()? nf.app : nf.aumid; bool need;
                  { std::lock_guard<std::mutex> lk(g_nIconMtx); need=!g_nIcons.count(key); }
                  if(need){
                      std::vector<uint8_t> buf;
                      // AppInfo/DisplayInfo/logo can each be NULL (a null WinRT call is an access violation,
                      // not an exception catch(...) sees - it crashed the shell at startup)
                      try { auto ai=n.AppInfo(); auto di= ai? ai.DisplayInfo() : nullptr;
                            auto ref= di? di.GetLogo(winrt::Windows::Foundation::Size(96,96)) : nullptr;
                            auto st= ref? ref.OpenReadAsync().get() : nullptr; uint64_t sz= st? st.Size() : 0;
                            if(sz>0 && sz<8000000){ winrt::Windows::Storage::Streams::DataReader dr(st); dr.LoadAsync((uint32_t)sz).get();
                                buf.resize((size_t)sz); dr.ReadBytes(winrt::array_view<uint8_t>(buf.data(),buf.data()+buf.size())); } } catch(...){ buf.clear(); }
                      std::lock_guard<std::mutex> lk(g_nIconMtx); g_nIcons[key]=nullptr;
                      if(!buf.empty()) g_nIconBytes.emplace_back(key,std::move(buf)); } }
                auto it=seen.find(nf.id);
                if(it!=seen.end()) nf.at=it->second;                 // already known: keep its age
                else { nf.at = firstPoll? 0 : now;                   // brand new (0 = "from before we started")
                       if(!firstPoll) arrived=true; }
                fresh[nf.id]=nf.at;
                list.push_back(std::move(nf));
            }
            seen.swap(fresh);                                        // drops ids that went away
            // Windows hands them back oldest first, so a new toast landed at the BOTTOM, past the "+N more"
            // cap - measured: two fresh test toasts never appeared behind 34 older ones. Newest first.
            std::sort(list.begin(),list.end(),[](const Notif& a,const Notif& b){ return a.id>b.id; });
            bool changed;
            { std::lock_guard<std::mutex> lk(g_notifMtx);
              changed = (g_notifs.size()!=list.size());
              g_notifs=std::move(list); }
            if(arrived) g_notifChanged=GetTickCount64();              // ONLY a genuinely new toast pops the panel
            if((changed||arrived) && g_notifWake) PostMessageW(g_notifWake,WM_NULL,0,0);
            firstPoll=false;
        } catch(...) {}
        Sleep(1000);
    }
}
