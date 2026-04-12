// rewriter/mod.cpp — middleware: typo correction, please→sudo, custom rules
#include "../../API/ModdingAPI.hpp"
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

class RewriterMod : public Mod {
    std::map<std::string,std::string> typos_ = {
        {"sl",     "ls"},   {"gti",    "git"},   {"gut",    "git"},
        {"pythno", "python3"}, {"pytohn","python3"}, {"mkidr", "mkdir"},
        {"gerp",   "grep"}, {"grpe",   "grep"},  {"naon",   "nano"},
    };
    std::map<std::string,std::string> custom_;
    std::vector<std::string> autoSudo_ = {"apt","apt-get","pacman","dnf","yum"};
    bool logRewrites_ = true;

    void logRewrite(const std::string& from, const std::string& to) {
        if (logRewrites_)
            shell->print("%dim[rewriter] " + from + " → " + to + "%reset");
    }

    void saveCustom() {
        std::string data;
        for (auto& [k,v] : custom_) data += k + "=" + v + "\n";
        shell->storePut("custom_rewrites", data);
    }

    void loadCustom() {
        std::string data = shell->storeGet("custom_rewrites", "");
        std::istringstream ss(data);
        std::string line;
        while (std::getline(ss, line)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos)
                custom_[line.substr(0,eq)] = line.substr(eq+1);
        }
    }

public:
    RewriterMod() {
        meta.id          = "com.example.rewriter";
        meta.name        = "rewriter";
        meta.version     = "1.0.0";
        meta.author      = "example";
        meta.description = "Middleware: typo correction, please→sudo, custom rewrites";
    }

    ModSecurityPolicy securityPolicy() const override {
        ModSecurityPolicy p;
        p.allowTSHExpansion = true;
        p.allowStoreWrite   = true;
        return p;
    }

    int Init() override {
        loadCustom();

        shell->registerMiddleware([this](MiddlewareContext& ctx) {
            if (ctx.args.empty()) return;
            std::string& head = ctx.args[0];

            // 1. "please <cmd>" → "sudo <cmd>"
            if (head == "please" && ctx.args.size() > 1) {
                std::string newHead = ctx.args[1];
                ctx.args.erase(ctx.args.begin());
                ctx.args.insert(ctx.args.begin(), "sudo");
                logRewrite("please " + newHead, "sudo " + newHead);
                return;
            }
            // 2. Custom user rewrites
            { auto it = custom_.find(head);
              if (it != custom_.end()) { logRewrite(head, it->second); head = it->second; return; } }
            // 3. Built-in typo corrections
            { auto it = typos_.find(head);
              if (it != typos_.end()) { logRewrite(head, it->second); head = it->second; return; } }
            // 4. Auto-sudo for package managers (not already root)
            for (auto& c : autoSudo_) {
                if (head == c && getuid() != 0) {
                    ctx.args.insert(ctx.args.begin(), "sudo");
                    logRewrite(head, "sudo " + head);
                    return;
                }
            }
        });

        TshCommand cmd;
        static const std::string kHelp =
            "rewriter list|add <f> <t>|remove <f>|typos|log on|off";
        cmd.name     = "rewriter";
        cmd.helpText = kHelp;
        cmd.run = [this](int argc, char** argv) -> int {
            static const std::string help =
                "rewriter list|add <f> <t>|remove <f>|typos|log on|off";
            std::string sub = argc >= 2 ? std::string(argv[1]) : "list";
            if (sub == "list") {
                shell->print("%byellow=== Custom rewrites ===%reset");
                if (custom_.empty()) shell->print("  (none)");
                for (auto& [k,v] : custom_)
                    shell->print("  " + k + "  →  %bgreen" + v + "%reset");
                shell->print("%byellow=== Auto-sudo ===%reset");
                for (auto& c : autoSudo_) shell->print("  " + c);
            } else if (sub == "add" && argc >= 4) {
                custom_[argv[2]] = argv[3];
                saveCustom();
                shell->print("rewriter: added " + std::string(argv[2]) + " → " + argv[3]);
            } else if (sub == "remove" && argc >= 3) {
                custom_.erase(argv[2]);
                saveCustom();
                shell->print("rewriter: removed " + std::string(argv[2]));
            } else if (sub == "typos") {
                shell->print("%byellow=== Built-in typo corrections ===%reset");
                for (auto& [k,v] : typos_)
                    shell->print("  " + k + "  →  %bgreen" + v + "%reset");
            } else if (sub == "log" && argc >= 3) {
                logRewrites_ = (std::string(argv[2]) == "on");
                shell->print(std::string("rewriter: logging ") + (logRewrites_ ? "on" : "off"));
            } else {
                shell->print(help);
            }
            return 0;
        };
        shell->registerTCMD(cmd);
        return 0;
    }

    int Start()              override { return 0; }
    int Execute(int, char**) override { return 0; }
};

TSH_MOD_EXPORT(RewriterMod)
