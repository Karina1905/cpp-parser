#pragma once
#include <string>
#include <vector>

struct Node {
    std::string type;
    std::string value;
    std::vector<Node*> kids;
    int line = 0;

    Node(const std::string& t, const std::string& v = "", int ln = 0)
        : type(t), value(v), line(ln) {}

    Node* add(Node* k) { if (k) kids.push_back(k); return this; }
    Node* at(size_t i) const { return i < kids.size() ? kids[i] : nullptr; }
};

Node* mk(const std::string& t, const std::string& v = "");
Node* mk1(const std::string& t, Node* a);
Node* mk2(const std::string& t, const std::string& v, Node* a, Node* b);
Node* list_append(Node* list, Node* item);

std::string ast_to_json(const Node* n, int indent = 0);
void run_program(Node* program);
std::string gen_python(Node* program);
std::vector<std::string> check_program(Node* program);

extern int yylineno;
