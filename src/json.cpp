#include "ast.hpp"
#include <sstream>

static std::string esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\t': r += "\\t";  break;
            default:   r += c;
        }
    }
    return r;
}

std::string ast_to_json(const Node* n, int indent) {
    std::string pad(indent * 2, ' ');
    std::ostringstream o;
    o << pad << "{\"type\":\"" << esc(n->type) << "\"";
    if (!n->value.empty()) o << ",\"value\":\"" << esc(n->value) << "\"";
    if (n->line) o << ",\"line\":" << n->line;
    if (!n->kids.empty()) {
        o << ",\"children\":[\n";
        for (size_t i = 0; i < n->kids.size(); ++i) {
            o << ast_to_json(n->kids[i], indent + 1);
            if (i + 1 < n->kids.size()) o << ",";
            o << "\n";
        }
        o << pad << "]";
    }
    o << "}";
    return o.str();
}
