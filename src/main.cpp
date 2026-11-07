#include "ast.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

extern FILE* yyin;
extern int yyparse();
extern Node* ast_root;

static void usage(const char* prog) {
    std::cerr <<
        "Usage: " << prog << " <source.cpp> [options]\n"
        "  --run              execute the program (interpreter)\n"
        "  --gen <out.py>     translate to Python\n"
        "  --ast <out.json>   dump AST as JSON\n"
        "  --viz <out.js>     dump AST as JS (window.AST_DATA) for viz/index.html\n"
        "  --print            print AST as JSON to stdout\n"
        "  --no-sema          skip the static semantic check\n"
        "(no options: parse + check only)\n";
}

static void write_file(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << text;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    std::string src = argv[1];
    bool run = false, print = false, sema = true;
    std::string genPath, astPath, vizPath;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--run") run = true;
        else if (a == "--print") print = true;
        else if (a == "--no-sema") sema = false;
        else if (a == "--gen" && i + 1 < argc) genPath = argv[++i];
        else if (a == "--ast" && i + 1 < argc) astPath = argv[++i];
        else if (a == "--viz" && i + 1 < argc) vizPath = argv[++i];
        else { usage(argv[0]); return 1; }
    }

    yyin = fopen(src.c_str(), "r");
    if (!yyin) { std::cerr << "cannot open " << src << "\n"; return 1; }
    if (yyparse() != 0 || !ast_root) { std::cerr << "Parsing failed.\n"; return 2; }
    fclose(yyin);
    std::cerr << "[ok] parsed " << src << "\n";

    try {
        std::string json;
        if (print || !astPath.empty() || !vizPath.empty()) json = ast_to_json(ast_root);
        if (print) std::cout << json << "\n";
        if (!astPath.empty()) { write_file(astPath, json + "\n"); std::cerr << "[ok] AST -> " << astPath << "\n"; }
        if (!vizPath.empty()) { write_file(vizPath, "window.AST_DATA = " + json + ";\n"); std::cerr << "[ok] AST (JS) -> " << vizPath << "\n"; }
        if (sema) {
            std::vector<std::string> errs = check_program(ast_root);
            if (!errs.empty()) {
                for (const auto& e : errs) std::cerr << e << "\n";
                std::cerr << "Semantic check failed: " << errs.size() << " error(s).\n";
                return 4;
            }
            std::cerr << "[ok] semantic check passed\n";
        }
        if (!genPath.empty()) { write_file(genPath, gen_python(ast_root)); std::cerr << "[ok] Python -> " << genPath << "\n"; }
        if (run) { std::cerr << "[run]\n"; run_program(ast_root); }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 3;
    }
    return 0;
}
