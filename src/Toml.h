// =================================================================================================
// Toml.h - the config files are the source of truth, so this has to do something a normal parser
// does not: remember WHERE every key lives.
//
// A config you can share is a config you can comment. If saving a setting rewrote the file from the
// in-memory values, every comment, every blank line and every bit of alignment you put in would be
// gone the first time you moved a slider. So a TomlFile keeps the file's lines VERBATIM and records
// which line (and which span of that line) each key's value occupies. Writing a value splices the
// new text into that span and leaves the rest of the line - including a trailing `# comment` -
// exactly as it was.
//
// This is not general TOML. It covers what a config file needs:
//   [table]  and  [nested.table]
//   [[array]] tables, addressed as  array.0.key, array.1.key
//   key = value   with string / integer / float / bool / array values
//   # comments, anywhere, and blank lines
//   arrays that span several lines
// Anything else is preserved verbatim but not interpreted.
// =================================================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace Tml {

// where a value physically lives, so it can be replaced in place
struct Span { int line=-1; int col0=0; int endLine=-1; int col1=0; };

inline std::string Trim(const std::string& s){
    size_t a=s.find_first_not_of(" \t\r\n");
    if(a==std::string::npos) return std::string();
    size_t b=s.find_last_not_of(" \t\r\n");
    return s.substr(a,b-a+1);
}

// Walk a line from `i`, stopping at the first `#` that is NOT inside a string. Returns the index of
// the comment, or npos. Bracket depth matters too: a `#` inside an array literal still ends the
// value in TOML, so depth is only tracked to know when a multi-line array continues.
inline size_t CommentAt(const std::string& s, size_t i=0){
    bool inStr=false; char q=0;
    for(; i<s.size(); i++){
        char c=s[i];
        if(inStr){ if(c=='\\'){ i++; continue; } if(c==q) inStr=false; continue; }
        if(c=='"'||c=='\''){ inStr=true; q=c; continue; }
        if(c=='#') return i;
    }
    return std::string::npos;
}
// net bracket depth added by this text, ignoring brackets inside strings
inline int BracketDelta(const std::string& s){
    int d=0; bool inStr=false; char q=0;
    for(size_t i=0;i<s.size();i++){
        char c=s[i];
        if(inStr){ if(c=='\\'){ i++; continue; } if(c==q) inStr=false; continue; }
        if(c=='"'||c=='\''){ inStr=true; q=c; continue; }
        if(c=='#') break;
        if(c=='[') d++; else if(c==']') d--;
    }
    return d;
}

struct File {
    std::string              path;
    std::vector<std::string> lines;      // verbatim
    std::map<std::string,Span>        where;   // "bar.size" -> span of its value text
    std::map<std::string,std::string> raw;     // "bar.size" -> value text, comment stripped
    std::map<std::string,int>         tableLine;  // "bar" -> line index of its [bar] header
    std::vector<std::string>          tableOrder;
    bool loaded=false;
    bool dirty=false;

    void clear(){ lines.clear(); where.clear(); raw.clear(); tableLine.clear(); tableOrder.clear();
                  loaded=false; dirty=false; }

    // ------------------------------------------------------------------ parse
    bool parse(const std::string& text){
        clear();
        { std::istringstream is(text); std::string ln;
          while(std::getline(is,ln)){ if(!ln.empty() && ln.back()=='\r') ln.pop_back(); lines.push_back(ln); } }
        std::string table;                       // current table path, "" = root
        std::map<std::string,int> arrayCount;    // [[x]] -> how many seen
        for(int i=0;i<(int)lines.size();i++){
            const std::string& L=lines[i];
            std::string t=Trim(L.substr(0, CommentAt(L)==std::string::npos? L.size() : CommentAt(L)));
            if(t.empty()) continue;
            if(t.size()>=2 && t.front()=='[' && t.back()==']'){
                if(t.size()>=4 && t[1]=='[' && t[t.size()-2]==']'){        // [[array]]
                    std::string name=Trim(t.substr(2,t.size()-4));
                    int n=arrayCount[name]++;
                    table=name+"."+std::to_string(n);
                } else {                                                   // [table]
                    table=Trim(t.substr(1,t.size()-2));
                    if(!tableLine.count(table)){ tableLine[table]=i; tableOrder.push_back(table); }
                }
                continue;
            }
            size_t eq=t.find('=');
            if(eq==std::string::npos) continue;
            std::string key=Trim(t.substr(0,eq));
            if(key.empty()) continue;
            if(key.size()>=2 && key.front()=='"' && key.back()=='"') key=key.substr(1,key.size()-2);
            std::string full = table.empty()? key : table+"."+key;

            // locate the value span on the ORIGINAL line (t was trimmed, so find `=` again there)
            size_t eqL = L.find('=', L.find(Trim(t.substr(0,eq))));
            if(eqL==std::string::npos) eqL = L.find('=');
            size_t v0 = eqL+1;
            while(v0<L.size() && (L[v0]==' '||L[v0]=='\t')) v0++;
            size_t cm = CommentAt(L, v0);
            size_t v1 = (cm==std::string::npos)? L.size() : cm;
            while(v1>v0 && (L[v1-1]==' '||L[v1-1]=='\t')) v1--;

            Span sp; sp.line=i; sp.col0=(int)v0; sp.endLine=i; sp.col1=(int)v1;
            std::string value=L.substr(v0, v1-v0);

            // a value that opens a bracket and does not close it keeps going on later lines
            int depth=BracketDelta(value);
            int j=i;
            while(depth>0 && j+1<(int)lines.size()){
                j++;
                const std::string& N=lines[j];
                size_t nc=CommentAt(N);
                std::string body = (nc==std::string::npos)? N : N.substr(0,nc);
                value += " " + Trim(body);
                depth += BracketDelta(N);
                size_t e=(nc==std::string::npos)? N.size() : nc;
                while(e>0 && (N[e-1]==' '||N[e-1]=='\t')) e--;
                sp.endLine=j; sp.col1=(int)e;
            }
            i=j;
            where[full]=sp;
            raw[full]=Trim(value);
        }
        loaded=true;
        return true;
    }

    bool load(const std::string& p){
        path=p;
        std::ifstream f(p, std::ios::binary);
        if(!f){ clear(); path=p; return false; }
        std::stringstream ss; ss<<f.rdbuf();
        return parse(ss.str());
    }

    bool save() const {
        if(path.empty()) return false;
        std::ofstream f(path, std::ios::binary);
        if(!f) return false;
        for(size_t i=0;i<lines.size();i++){ f<<lines[i]; if(i+1<lines.size()||true) f<<"\n"; }
        return true;
    }

    // ------------------------------------------------------------------ read
    bool has(const std::string& k) const { return raw.count(k)!=0; }
    std::string rawOf(const std::string& k) const {
        auto it=raw.find(k); return it==raw.end()? std::string() : it->second; }

    static std::string Unquote(const std::string& v){
        std::string s=Trim(v);
        if(s.size()>=2 && ((s.front()=='"'&&s.back()=='"')||(s.front()=='\''&&s.back()=='\''))){
            std::string o; char q=s.front();
            for(size_t i=1;i+1<s.size();i++){
                if(q=='"' && s[i]=='\\' && i+2<s.size()){
                    char c=s[++i];
                    o += (c=='n')?'\n' : (c=='t')?'\t' : c;
                } else o+=s[i];
            }
            return o;
        }
        return s;
    }
    std::string str(const std::string& k,const std::string& d="") const {
        if(!has(k)) return d; return Unquote(rawOf(k)); }
    double num(const std::string& k,double d=0) const {
        if(!has(k)) return d;
        std::string s=Trim(rawOf(k)); if(s.empty()) return d;
        char* end=nullptr; double v=strtod(s.c_str(),&end);
        return (end==s.c_str())? d : v; }
    bool boolean(const std::string& k,bool d=false) const {
        if(!has(k)) return d;
        std::string s=Trim(rawOf(k));
        if(s=="true"||s=="1") return true;
        if(s=="false"||s=="0") return false;
        return d; }
    // split an inline array's elements, respecting quotes and nesting
    std::vector<std::string> array(const std::string& k) const {
        std::vector<std::string> out;
        if(!has(k)) return out;
        std::string s=Trim(rawOf(k));
        if(s.size()<2 || s.front()!='[' || s.back()!=']') return out;
        s=s.substr(1,s.size()-2);
        int depth=0; bool inStr=false; char q=0; std::string cur;
        for(size_t i=0;i<s.size();i++){
            char c=s[i];
            if(inStr){ cur+=c; if(c=='\\'&&i+1<s.size()){ cur+=s[++i]; continue; } if(c==q) inStr=false; continue; }
            if(c=='"'||c=='\''){ inStr=true; q=c; cur+=c; continue; }
            if(c=='['){ depth++; cur+=c; continue; }
            if(c==']'){ depth--; cur+=c; continue; }
            if(c==',' && depth==0){ std::string e=Trim(cur); if(!e.empty()) out.push_back(e); cur.clear(); continue; }
            cur+=c;
        }
        { std::string e=Trim(cur); if(!e.empty()) out.push_back(e); }
        return out;
    }
    std::vector<std::string> strArray(const std::string& k) const {
        std::vector<std::string> v=array(k);
        for(auto& e:v) e=Unquote(e);
        return v;
    }
    // how many [[name]] entries were parsed
    int arrayTables(const std::string& name) const {
        int n=0; while(tableLineOf(name+"."+std::to_string(n))>=-1 && hasAnyUnder(name+"."+std::to_string(n))) n++;
        return n;
    }
    bool hasAnyUnder(const std::string& prefix) const {
        auto it=raw.lower_bound(prefix+".");
        return it!=raw.end() && it->first.compare(0,prefix.size()+1,prefix+".")==0;
    }
    int tableLineOf(const std::string& t) const {
        auto it=tableLine.find(t); return it==tableLine.end()? -1 : it->second; }

    // ------------------------------------------------------------------ write in place
    // Replaces ONLY the value text. Indentation, the spaces around `=`, and any trailing comment
    // survive untouched - that is the entire point of this file existing.
    bool set(const std::string& k, const std::string& valueText){
        auto it=where.find(k);
        if(it!=where.end()){
            Span sp=it->second;
            if(sp.line<0 || sp.line>=(int)lines.size()) return false;
            if(sp.endLine!=sp.line){
                // multi-line array: collapse it onto the first line, keeping that line's prefix and
                // dropping the continuation lines it owned
                std::string head=lines[sp.line].substr(0,sp.col0);
                std::string tailComment;
                { const std::string& last=lines[sp.endLine];
                  size_t cm=CommentAt(last, 0);
                  if(cm!=std::string::npos && (int)cm>=sp.col1) tailComment=" "+last.substr(cm); }
                lines[sp.line]=head+valueText+tailComment;
                lines.erase(lines.begin()+sp.line+1, lines.begin()+sp.endLine+1);
                reindex();
            } else {
                std::string& L=lines[sp.line];
                L = L.substr(0,sp.col0) + valueText + L.substr(sp.col1);
                Span n=sp; n.col1=sp.col0+(int)valueText.size(); n.endLine=n.line;
                where[k]=n;
                raw[k]=Trim(valueText);
            }
            dirty=true;
            return true;
        }
        // not present: append it under its table, creating the table if needed
        size_t dot=k.rfind('.');
        std::string table = (dot==std::string::npos)? std::string() : k.substr(0,dot);
        std::string key   = (dot==std::string::npos)? k : k.substr(dot+1);
        int hdr = table.empty()? -1 : tableLineOf(table);
        if(!table.empty() && hdr<0){
            if(!lines.empty() && !Trim(lines.back()).empty()) lines.push_back("");
            lines.push_back("["+table+"]");
            hdr=(int)lines.size()-1;
            tableLine[table]=hdr; tableOrder.push_back(table);
        }
        int insertAt;
        if(hdr<0) insertAt=(int)lines.size();
        else {
            insertAt=hdr+1;
            for(int i=hdr+1;i<(int)lines.size();i++){
                std::string t=Trim(lines[i]);
                if(!t.empty() && t.front()=='[') break;      // next table starts
                insertAt=i+1;
            }
            while(insertAt>hdr+1 && Trim(lines[insertAt-1]).empty()) insertAt--;
        }
        lines.insert(lines.begin()+insertAt, key+" = "+valueText);
        reindex();
        dirty=true;
        return true;
    }
    // after an insert/erase every recorded line index below the change is stale
    void reindex(){
        std::vector<std::string> keep=lines;
        parse([&]{ std::string s; for(size_t i=0;i<keep.size();i++){ s+=keep[i]; s+="\n"; } return s; }());
        lines=keep;
    }

    // ------------------------------------------------------------------ value formatting
    static std::string Quote(const std::string& s){
        std::string o="\"";
        for(char c:s){
            if(c=='\\'||c=='"'){ o+='\\'; o+=c; }
            else if(c=='\n') o+="\\n";
            else if(c=='\t') o+="\\t";
            else o+=c;
        }
        o+="\"";
        return o;
    }
    static std::string Num(double v){
        // integers stay integers so the file reads like a person wrote it
        if(v==(double)(long long)v && v<1e15 && v>-1e15) return std::to_string((long long)v);
        char b[48]; snprintf(b,sizeof(b),"%.4g",v); return b;
    }
    static std::string Bool(bool b){ return b?"true":"false"; }
    static std::string StrArray(const std::vector<std::string>& v){
        std::string o="[";
        for(size_t i=0;i<v.size();i++){ if(i) o+=", "; o+=Quote(v[i]); }
        o+="]"; return o;
    }
    static std::string NumArray(const std::vector<double>& v){
        std::string o="[";
        for(size_t i=0;i<v.size();i++){ if(i) o+=", "; o+=Num(v[i]); }
        o+="]"; return o;
    }
};

} // namespace Tml
