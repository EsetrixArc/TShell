// tshc — TShell Mod Compiler  v2.0
// =============================================================================
// Usage:
//   tshc new    <name>         scaffold a new C++ mod
//   tshc new    <name> --py   scaffold a Python mod
//   tshc build  <mod-dir>      compile mod.cpp → main.so, bundle as .tmod
//   tshc install <mod-dir>     build + install into Bin/Mods/
//   tshc unpack <file.tmod>    unpack a .tmod
//   tshc info   <file.tmod>    show manifest
//   tshc version
// =============================================================================

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
static const char* VERSION = "2.0.0";

static std::string jsonStr(const std::string& src,const std::string& key){
    std::string needle="\""+key+"\"";
    auto kp=src.find(needle); if(kp==std::string::npos) return "";
    auto colon=src.find(':',kp+needle.size()); if(colon==std::string::npos) return "";
    auto q1=src.find('"',colon+1); if(q1==std::string::npos) return "";
    auto q2=src.find('"',q1+1); if(q2==std::string::npos) return "";
    return src.substr(q1+1,q2-q1-1);}

static bool fileExists(const std::string& p){std::error_code ec;return fs::exists(p,ec);}
static int run(const std::string& cmd){std::cout<<"  $ "<<cmd<<"\n";return std::system(cmd.c_str());}

// ── new (C++) ──────────────────────────────────────────────────────────────

static int cmdNew(const std::string& name,bool python){
    if(fileExists(name)){std::cerr<<"tshc new: '"<<name<<"' already exists.\n";return 1;}
    std::error_code ec; fs::create_directory(name,ec);
    if(ec){std::cerr<<"tshc new: "<<ec.message()<<"\n";return 1;}

    // manifest.json
    {std::ofstream f(name+"/manifest.json");
     f<<"{\n";
     f<<"  \"id\": \"com.example."<<name<<"\",\n";
     f<<"  \"name\": \""<<name<<"\",\n";
     f<<"  \"version\": \"1.0.0\",\n";
     f<<"  \"author\": \"you\",\n";
     f<<"  \"description\": \"My "<<name<<" mod\",\n";
     if(python){
         f<<"  \"lang\": \"python\",\n";
         f<<"  \"entry\": \"mod.py\",\n";
     } else {
         f<<"  \"entry\": \"main.so\",\n";
     }
     f<<"  \"apiVersion\": 3\n";
     f<<"}\n";}

    if(python){
        // Python mod scaffold
        std::ofstream f(name+"/mod.py");
        f<<"#!/usr/bin/env python3\n";
        f<<"# "<<name<<" — TShell Python mod\n";
        f<<"# Invoked as: python3 mod.py [args...]\n";
        f<<"import sys\n\n";
        f<<"def main(args):\n";
        f<<"    print(f\""<<name<<" mod: called with {args}\")\n";
        f<<"    return 0\n\n";
        f<<"if __name__ == '__main__':\n";
        f<<"    sys.exit(main(sys.argv[1:]))\n";
        std::cout<<"Scaffolded Python mod: "<<name<<"/\n";
        std::cout<<"  "<<name<<"/manifest.json\n";
        std::cout<<"  "<<name<<"/mod.py\n";
        std::cout<<"\nRegister it by placing it in Bin/Mods/"<<name<<"/\n";
        return 0;
    }

    // C++ scaffold
    {std::ofstream f(name+"/mod.cpp");
     f<<"// "<<name<<" — TShell mod (API v3)\n";
     f<<"#include \"../../Bin/API/ModdingAPI.hpp\"\n";
     f<<"#include <iostream>\n\n";
     f<<"class "<<name<<"Mod : public Mod {\n";
     f<<"public:\n";
     f<<"    "<<name<<"Mod() {\n";
     f<<"        meta.id          = \"com.example."<<name<<"\";\n";
     f<<"        meta.name        = \""<<name<<"\";\n";
     f<<"        meta.version     = \"1.0.0\";\n";
     f<<"        meta.author      = \"you\";\n";
     f<<"        meta.description = \"My "<<name<<" mod\";\n";
     f<<"    }\n\n";
     f<<"    int Init() override {\n";
     f<<"        // Register a command:\n";
     f<<"        shell->registerCommand(\""<<name<<"\");\n\n";
     f<<"        // Register a theme:\n";
     f<<"        // shell->registerTheme(\"MyTheme\", \"prompt fmt\", \"description\");\n\n";
     f<<"        // Register a prompt token:\n";
     f<<"        // shell->registerToken(\"MYTOKEN\", []{ return std::string(\"value\"); });\n\n";
     f<<"        // Register a hook:\n";
     f<<"        // shell->registerHook(TshEvent::PostExec, [](TshHookContext& ctx) {\n";
     f<<"        //     if (ctx.exitCode != 0)\n";
     f<<"        //         ctx.cancel = false; // just observe\n";
     f<<"        // });\n\n";
     f<<"        // Register completions:\n";
     f<<"        // shell->registerCompletions(\""<<name<<"\", [](const std::string& partial,\n";
     f<<"        //     const std::vector<std::string>& argv) -> std::vector<CompletionEntry> {\n";
     f<<"        //         return {{\"option1\", \"first option\"}, {\"option2\", \"second\"}};\n";
     f<<"        // });\n\n";
     f<<"        // Persistent storage:\n";
     f<<"        // shell->storePut(\"mykey\", \"myvalue\");\n";
     f<<"        // std::string v = shell->storeGet(\"mykey\", \"default\");\n\n";
     f<<"        return 0;\n";
     f<<"    }\n\n";
     f<<"    int Start() override { return 0; }\n\n";
     f<<"    int Execute(int argc, char* argv[]) override {\n";
     f<<"        shell->print(\"%bcyan"<<name<<" mod%reset: Execute called\");\n";
     f<<"        return 0;\n";
     f<<"    }\n";
     f<<"};\n\n";
     f<<"TSH_MOD_EXPORT("<<name<<"Mod)\n";}

    {std::ofstream f(name+"/Makefile");
     f<<"CXX      := g++\n";
     f<<"CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -fPIC -pthread -I../../\n\n";
     f<<"all: main.so\n\n";
     f<<"main.so: mod.cpp\n";
     f<<"\t$(CXX) $(CXXFLAGS) -o $@ $^\n\n";
     f<<"clean:\n\trm -f main.so\n";}

    std::cout<<"Scaffolded C++ mod: "<<name<<"/\n";
    std::cout<<"  "<<name<<"/manifest.json\n";
    std::cout<<"  "<<name<<"/mod.cpp\n";
    std::cout<<"  "<<name<<"/Makefile\n";
    std::cout<<"\nEdit mod.cpp, then: tshc build "<<name<<"/\n";
    return 0;
}

// ── build ──────────────────────────────────────────────────────────────────

static int cmdBuild(const std::string& modDir,bool doInstall=false,
                    const std::string& installDir="Bin/Mods"){
    if(!fileExists(modDir)){std::cerr<<"tshc build: not found: "<<modDir<<"\n";return 1;}
    std::string clean=modDir;
    while(!clean.empty()&&clean.back()=='/') clean.pop_back();
    std::string modName=fs::path(clean).filename().string();

    // Read manifest to detect python mods
    std::string mfPath=clean+"/manifest.json";
    std::string lang="cpp";
    if(fileExists(mfPath)){
        std::ifstream f(mfPath); std::ostringstream ss; ss<<f.rdbuf();
        lang=jsonStr(ss.str(),"lang"); if(lang.empty()) lang="cpp";
    }

    if(lang=="python"){
        std::cout<<"Python mod: no compilation needed.\n";
        if(doInstall){
            std::error_code ec;
            std::string dest=installDir+"/"+modName;
            fs::create_directories(dest,ec);
            for(auto& e:fs::directory_iterator(clean,ec))
                fs::copy_file(e.path(),dest+"/"+e.path().filename().string(),
                              fs::copy_options::overwrite_existing,ec);
            std::cout<<"Installed to: "<<dest<<"/\n";
        }
        return 0;
    }

    // C++ mod
    std::string srcFile=clean+"/mod.cpp";
    std::string soFile =clean+"/main.so";

    if(!fileExists(srcFile)){
        if(fileExists(clean+"/Makefile")){
            if(run("make -C \""+clean+"\"")!=0){std::cerr<<"tshc build: make failed\n";return 1;}
        } else {
            std::cerr<<"tshc build: no mod.cpp or Makefile in "<<clean<<"\n";return 1;}
    } else {
        std::string cmd="g++ -std=c++17 -O2 -shared -fPIC -I. \""+srcFile+"\" -o \""+soFile+"\"";
        if(run(cmd)!=0){std::cerr<<"tshc build: compilation failed\n";return 1;}
    }

    if(!fileExists(soFile)){std::cerr<<"tshc build: no "<<soFile<<" produced\n";return 1;}

    // Bundle as .tmod
    std::string tmodFile=modName+".tmod";
    {std::string zipCmd="zip -q \""+tmodFile+"\"";
     if(fileExists(mfPath)) zipCmd+=" -j \""+mfPath+"\"";
     zipCmd+=" -j \""+soFile+"\"";
     run(zipCmd);}
    std::cout<<"\nBuilt: "<<tmodFile<<"\n";

    if(doInstall){
        std::error_code ec;
        std::string dest=installDir+"/"+modName;
        fs::create_directories(dest,ec);
        if(fileExists(mfPath)) fs::copy_file(mfPath,dest+"/manifest.json",
            fs::copy_options::overwrite_existing,ec);
        fs::copy_file(soFile,dest+"/main.so",fs::copy_options::overwrite_existing,ec);
        std::cout<<"Installed to: "<<dest<<"/\n";
    }
    return 0;
}

static int cmdUnpack(const std::string& f){
    if(!fileExists(f)){std::cerr<<"tshc unpack: not found: "<<f<<"\n";return 1;}
    std::string name=fs::path(f).stem().string();
    std::error_code ec; fs::create_directory(name,ec);
    return run("unzip -q \""+f+"\" -d \""+name+"\"")?1:0;
}

static int cmdInfo(const std::string& f){
    if(!fileExists(f)){std::cerr<<"tshc info: not found: "<<f<<"\n";return 1;}
    std::cout<<"=== "<<f<<" ===\n";
    return std::system(("unzip -p \""+f+"\" manifest.json").c_str());
}

static void usage(){
    std::cout<<"tshc v"<<VERSION<<" — TShell Mod Compiler\n\n";
    std::cout<<"Usage:\n";
    std::cout<<"  tshc new     <name>           scaffold C++ mod\n";
    std::cout<<"  tshc new     <name> --py      scaffold Python mod\n";
    std::cout<<"  tshc build   <mod-dir>         compile + bundle .tmod\n";
    std::cout<<"  tshc install <mod-dir>         build + install into Bin/Mods/\n";
    std::cout<<"  tshc unpack  <file.tmod>       extract\n";
    std::cout<<"  tshc info    <file.tmod>       show manifest\n";
    std::cout<<"  tshc version\n";
}

int main(int argc,char** argv){
    if(argc<2){usage();return 0;}
    std::string sub=argv[1];
    if(sub=="version"){std::cout<<"tshc v"<<VERSION<<"\n";return 0;}
    if(sub=="new"&&argc>=3){
        bool py=(argc>=4&&std::string(argv[3])=="--py");
        return cmdNew(argv[2],py);}
    if(sub=="build"&&argc>=3) return cmdBuild(argv[2],false);
    if(sub=="install"&&argc>=3){
        std::string dir=(argc>=4)?argv[3]:"Bin/Mods";
        return cmdBuild(argv[2],true,dir);}
    if(sub=="unpack"&&argc>=3) return cmdUnpack(argv[2]);
    if(sub=="info"&&argc>=3)   return cmdInfo(argv[2]);
    std::cerr<<"tshc: unknown command '"<<sub<<"'\n\n";
    usage(); return 1;
}