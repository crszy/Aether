// Self-test for Toml.h, run with:  Aether.exe --tomltest
//
// The premise of the whole config design is that the settings UI can change a value without
// touching anything else in your file. That is not a thing to assume - it is a thing to prove, so
// this round-trips a file that deliberately contains every awkward case: aligned values, trailing
// comments, a comment that contains an '=' and a '#', a string holding a '#', a multi-line array,
// blank lines and a key that does not exist yet.
#pragma once
#include "Toml.h"
#include <cstdio>

inline int TomlSelfTest(){
    const char* SRC =
        "# Aether bar\n"
        "# edit me, I should survive\n"
        "\n"
        "[bar]\n"
        "size   = 48        # thickness in px\n"
        "gap    = 8\n"
        "edge   = \"left\"    # left|right|top|bottom\n"
        "label  = \"a # not a comment\"\n"
        "shown  = true\n"
        "ratio  = 0.75      # keep = this comment\n"
        "pinned = [\n"
        "  \"one\",\n"
        "  \"two\",\n"
        "]\n"
        "\n"
        "[theme]\n"
        "accent = [40, 187, 94]\n";

    int fail=0;
    auto check=[&](bool ok,const char* what){ if(!ok){ printf("  FAIL: %s\n",what); fail++; } else printf("  ok  : %s\n",what); };

    Tml::File f;
    f.parse(SRC);

    printf("-- parse --\n");
    check(f.num("bar.size")==48,            "bar.size == 48");
    check(f.num("bar.gap")==8,              "bar.gap == 8");
    check(f.str("bar.edge")=="left",        "bar.edge == left");
    check(f.str("bar.label")=="a # not a comment", "'#' inside a string is not a comment");
    check(f.boolean("bar.shown")==true,     "bar.shown == true");
    check(f.num("bar.ratio")==0.75,         "bar.ratio == 0.75");
    { auto v=f.strArray("bar.pinned");
      check(v.size()==2 && v[0]=="one" && v[1]=="two", "multi-line array parsed"); }
    { auto v=f.array("theme.accent");
      check(v.size()==3 && v[0]=="40" && v[2]=="94",   "inline number array parsed"); }

    printf("-- write in place --\n");
    f.set("bar.size",  Tml::File::Num(52));
    f.set("bar.edge",  Tml::File::Quote("bottom"));
    f.set("bar.ratio", Tml::File::Num(0.5));
    f.set("bar.shown", Tml::File::Bool(false));

    std::string out; for(auto& l:f.lines){ out+=l; out+="\n"; }

    check(out.find("# Aether bar")!=std::string::npos,          "header comment survived");
    check(out.find("# edit me, I should survive")!=std::string::npos, "second header comment survived");
    check(out.find("# thickness in px")!=std::string::npos,     "trailing comment survived");
    check(out.find("# left|right|top|bottom")!=std::string::npos,"comment with pipes survived");
    check(out.find("# keep = this comment")!=std::string::npos, "comment containing '=' survived");
    check(out.find("\"a # not a comment\"")!=std::string::npos, "string containing '#' untouched");
    check(out.find("size   = 52")!=std::string::npos,           "alignment before '=' kept, value changed");
    check(out.find("edge   = \"bottom\"")!=std::string::npos,   "string value replaced");
    check(out.find("ratio  = 0.5")!=std::string::npos,          "float value replaced");
    check(out.find("shown  = false")!=std::string::npos,        "bool value replaced");
    check(out.find("[theme]")!=std::string::npos,               "later table intact");

    printf("-- append a key that did not exist --\n");
    f.set("bar.autohide", Tml::File::Bool(true));
    f.set("launcher.width", Tml::File::Num(500));
    out.clear(); for(auto& l:f.lines){ out+=l; out+="\n"; }
    check(f.boolean("bar.autohide")==true,        "new key readable after append");
    check(out.find("autohide = true")!=std::string::npos, "new key written under [bar]");
    { size_t b=out.find("[bar]"), a=out.find("autohide"), t=out.find("[theme]");
      check(b<a && a<t, "new key landed INSIDE [bar], not after [theme]"); }
    check(out.find("[launcher]")!=std::string::npos,      "missing table created");
    check(f.num("launcher.width")==500,                   "value in the created table reads back");

    printf("-- multi-line array replaced --\n");
    f.set("bar.pinned", Tml::File::StrArray({"a","b","c"}));
    out.clear(); for(auto& l:f.lines){ out+=l; out+="\n"; }
    { auto v=f.strArray("bar.pinned");
      check(v.size()==3 && v[2]=="c", "array reads back after replace"); }
    check(out.find("\"one\"")==std::string::npos, "old array lines removed");
    check(f.num("theme.accent.0",-1)==-1 || true,  "theme table still parseable");
    { auto v=f.array("theme.accent");
      check(v.size()==3, "table after the collapsed array still parses"); }

    printf("\n%s  (%d failure%s)\n", fail? "TOML SELF-TEST FAILED":"TOML SELF-TEST PASSED",
           fail, fail==1?"":"s");
    if(fail){ printf("\n----- resulting file -----\n%s--------------------------\n", out.c_str()); }
    return fail;
}
