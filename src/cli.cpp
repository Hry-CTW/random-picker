// 命令行版：无 GUI 环境可用，也用于验证解析与随机逻辑
#include <iostream>
#include <string>

#include "roster.h"

static void help() {
    std::cout << "用法:\n"
              << "  picker_cli import <文件> [--append]   导入名单(csv/txt/md/xlsx)\n"
              << "  picker_cli list                       列出名单与分组\n"
              << "  picker_cli pick [次数] [--group 组名] [--no-repeat]\n"
              << "  picker_cli pickgroup                  随机抽一个组\n"
              << "  picker_cli save <文件.csv>             保存名单\n"
              << "  picker_cli hist <文件.csv>             导出抽取记录\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        help();
        return 0;
    }
    std::string cmd = argv[1];
    Roster r;
    if (cmd == "import" && argc >= 3) {
        // 先尝试读取同名缓存，这里 CLI 单次执行，直接导入
        bool append = false;
        for (int i = 3; i < argc; i++)
            if (std::string(argv[i]) == "--append") append = true;
        std::wstring msg;
        bool ok = r.importFile(argv[2], append, msg);
        std::cout << (ok ? "OK  " : "FAIL ") << wide_to_utf8(msg) << "\n";
        if (!ok) return 1;
        std::cout << "--- 名单 ---\n";
        for (size_t i = 0; i < r.people.size(); i++)
            std::cout << i + 1 << ". " << wide_to_utf8(displayName(r.people[i])) << " ["
                      << wide_to_utf8(r.people[i].group) << "] " << wide_to_utf8(r.people[i].sid) << "\n";
        std::cout << "--- 分组 ---\n";
        for (auto& g : r.groupList()) std::cout << "  " << wide_to_utf8(g) << "\n";
        return 0;
    }
    if (cmd == "pick") {
        int n = (argc >= 3 && argv[2][0] >= '0' && argv[2][0] <= '9') ? atoi(argv[2]) : 1;
        std::string wantGroup;
        for (int i = 2; i < argc; i++) {
            if (std::string(argv[i]) == "--group" && i + 1 < argc) wantGroup = argv[i + 1];
            if (std::string(argv[i]) == "--no-repeat") r.noRepeat = true;
        }
        std::wstring msg;
        if (!r.importFile("people.csv", false, msg)) {
            std::cout << "请先提供 people.csv（或改用 GUI 导入）\n";
            return 1;
        }
        int scope = -1;
        if (!wantGroup.empty()) {
            auto gs = r.groupList();
            auto w = utf8_to_wide(wantGroup);
            for (size_t i = 0; i < gs.size(); i++)
                if (gs[i] == w) scope = (int)i;
        }
        for (int i = 0; i < n; i++) {
            PickResult p = r.pick(scope);
            if (!p.ok) {
                std::cout << "无可抽取对象\n";
                break;
            }
            std::cout << wide_to_utf8(displayName(p.person)) << " [" << wide_to_utf8(p.person.group) << "] seed="
                      << wide_to_utf8(p.seed) << "\n";
        }
        return 0;
    }
    if (cmd == "pickgroup") {
        std::wstring msg;
        if (!r.importFile("people.csv", false, msg)) {
            std::cout << "请先提供 people.csv\n";
            return 1;
        }
        PickResult p = r.pickGroup();
        std::cout << (p.ok ? wide_to_utf8(displayName(p.person)) : std::string("无分组")) << "\n";
        return 0;
    }
    help();
    return 0;
}
