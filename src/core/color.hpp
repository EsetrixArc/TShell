#pragma once
#include <map>
#include <string>

namespace Color {
#ifdef TSH_NO_COLOR
    constexpr const char* RESET    = "";
    constexpr const char* BOLD     = "";
    constexpr const char* DIM      = "";
    constexpr const char* BLACK    = "";
    constexpr const char* RED      = "";
    constexpr const char* GREEN    = "";
    constexpr const char* YELLOW   = "";
    constexpr const char* BLUE     = "";
    constexpr const char* MAGENTA  = "";
    constexpr const char* CYAN     = "";
    constexpr const char* WHITE    = "";
    constexpr const char* BRED     = "";
    constexpr const char* BGREEN   = "";
    constexpr const char* BYELLOW  = "";
    constexpr const char* BBLUE    = "";
    constexpr const char* BMAGENTA = "";
    constexpr const char* BCYAN    = "";
    constexpr const char* BWHITE   = "";
#else
    constexpr const char* RESET    = "\033[0m";
    constexpr const char* BOLD     = "\033[1m";
    constexpr const char* DIM      = "\033[2m";
    constexpr const char* BLACK    = "\033[30m";
    constexpr const char* RED      = "\033[31m";
    constexpr const char* GREEN    = "\033[32m";
    constexpr const char* YELLOW   = "\033[33m";
    constexpr const char* BLUE     = "\033[34m";
    constexpr const char* MAGENTA  = "\033[35m";
    constexpr const char* CYAN     = "\033[36m";
    constexpr const char* WHITE    = "\033[37m";
    constexpr const char* BRED     = "\033[91m";
    constexpr const char* BGREEN   = "\033[92m";
    constexpr const char* BYELLOW  = "\033[93m";
    constexpr const char* BBLUE    = "\033[94m";
    constexpr const char* BMAGENTA = "\033[95m";
    constexpr const char* BCYAN    = "\033[96m";
    constexpr const char* BWHITE   = "\033[97m";
#endif

    inline const std::map<std::string,std::string>& table() {
        static const std::map<std::string,std::string> t = {
            {"%reset",RESET},{"%bold",BOLD},{"%dim",DIM},
            {"%black",BLACK},{"%red",RED},{"%green",GREEN},{"%yellow",YELLOW},
            {"%blue",BLUE},{"%magenta",MAGENTA},{"%cyan",CYAN},{"%white",WHITE},
            {"%bred",BRED},{"%bgreen",BGREEN},{"%byellow",BYELLOW},
            {"%bblue",BBLUE},{"%bmagenta",BMAGENTA},{"%bcyan",BCYAN},{"%bwhite",BWHITE},
        };
        return t;
    }

    inline std::string apply(std::string s) {
        for (auto& [tok,code] : table()) {
            size_t p = 0;
            while ((p = s.find(tok, p)) != std::string::npos) {
                s.replace(p, tok.size(), code);
                p += code.size();
            }
        }
        return s;
    }

    inline std::string strip(std::string s) {
        for (auto& [tok, _] : table()) {
            size_t p = 0;
            while ((p = s.find(tok, p)) != std::string::npos)
                s.replace(p, tok.size(), "");
        }
        return s;
    }
}
