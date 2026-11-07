#include "ast.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

using std::string;

namespace {

struct ClassInfo {
    string base;
    std::map<string, string> fields;
    std::map<string, Node*> methods;
    std::set<string> protos;
    std::vector<size_t> ctorArity;
    std::vector<Node*> ctors;
};

struct Sema {
    std::vector<string> errors;
    std::map<string, ClassInfo> classes;
    std::map<string, Node*> functions;
    std::map<string, string> globals;
    std::vector<std::map<string, string>> scopes;

    string curClass;
    string curReturn;
    string curFunc;
    int loopDepth = 0;
    bool inFunction = false;

    void error(Node* n, const string& msg) {
        errors.push_back("Semantic error" + (n && n->line ? " (line " + std::to_string(n->line) + ")" : "") + ": " + msg);
    }

    static bool is_numeric(const string& t) { return t == "int" || t == "double" || t == "bool" || t == "char"; }
    static bool is_array(const string& t) { return t.size() > 2 && t.substr(t.size() - 2) == "[]"; }
    static string elem(const string& t) { return is_array(t) ? t.substr(0, t.size() - 2) : ""; }
    bool is_class(const string& t) const { return classes.count(t) > 0; }
    bool type_exists(const string& t) const { return is_numeric(t) || t == "string" || t == "void" || is_class(t); }

    bool compatible(const string& target, const string& src) const {
        if (target.empty() || src.empty()) return true;
        if (target == src) return true;
        if (is_numeric(target) && is_numeric(src)) return true;
        if (target == "string" && src == "char") return true;
        if (is_class(target) && is_class(src)) {
            for (string c = src; !c.empty(); c = classes.at(c).base) if (c == target) return true;
        }
        return false;
    }

    string field_type(const string& cls, const string& name) const {
        for (string c = cls; !c.empty(); c = classes.at(c).base) {
            auto f = classes.at(c).fields.find(name);
            if (f != classes.at(c).fields.end()) return f->second;
            if (!classes.count(classes.at(c).base)) break;
        }
        return "";
    }
    Node* find_method(const string& cls, const string& name) const {
        for (string c = cls; !c.empty(); c = classes.at(c).base) {
            auto m = classes.at(c).methods.find(name);
            if (m != classes.at(c).methods.end()) return m->second;
            if (!classes.count(classes.at(c).base)) break;
        }
        return nullptr;
    }
    bool has_proto(const string& cls, const string& name) const {
        return classes.count(cls) && classes.at(cls).protos.count(name);
    }

    void push() { scopes.emplace_back(); }
    void pop() { scopes.pop_back(); }
    string lookup(const string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return f->second;
        }
        if (!curClass.empty()) { string t = field_type(curClass, name); if (!t.empty()) return t; }
        auto g = globals.find(name);
        if (g != globals.end()) return g->second;
        return "";
    }
    bool declared_anywhere(const string& name) {
        return !lookup(name).empty();
    }
    void declare(Node* n, const string& name, const string& type) {
        if (scopes.back().count(name)) { error(n, "redeclaration of '" + name + "'"); return; }
        if (scopes.size() == 1 && functions.count(name)) error(n, "'" + name + "' is already a function");
        scopes.back()[name] = type;
    }

    void collect(Node* prog) {
        for (Node* d : prog->kids) {
            if (d->type == "ClassDecl") {
                if (classes.count(d->value)) error(d, "class '" + d->value + "' is already defined");
                ClassInfo ci;
                for (Node* m : d->kids) {
                    if (m->type == "Base") ci.base = m->value;
                    else if (m->type == "Field") {
                        if (ci.fields.count(m->value)) error(m, "duplicate field '" + m->value + "' in class " + d->value);
                        ci.fields[m->value] = m->at(0)->value + (m->at(1) && m->at(1)->type == "Dim" ? "[]" : "");
                    } else if (m->type == "Method") {
                        if (ci.methods.count(m->value)) error(m, "duplicate method '" + m->value + "' in class " + d->value);
                        ci.methods[m->value] = m;
                    } else if (m->type == "MethodProto") ci.protos.insert(m->value);
                    else if (m->type == "Ctor") {
                        if (m->value != d->value) error(m, "constructor name '" + m->value + "' does not match class " + d->value);
                        size_t ar = m->at(0)->kids.size();
                        for (size_t a : ci.ctorArity) if (a == ar) error(m, "duplicate constructor with " + std::to_string(ar) + " parameter(s)");
                        ci.ctorArity.push_back(ar);
                        ci.ctors.push_back(m);
                    }
                }
                classes[d->value] = ci;
            }
        }
        for (auto& kv : classes) {
            if (!kv.second.base.empty() && !classes.count(kv.second.base))
                error(nullptr, "class " + kv.first + ": unknown base class '" + kv.second.base + "'");
        }
        for (Node* d : prog->kids) {
            if (d->type == "FuncDecl" || d->type == "FuncProto") {
                if (functions.count(d->value) && d->type == "FuncDecl" && functions[d->value]->type == "FuncDecl")
                    error(d, "function '" + d->value + "' is already defined");
                if (!functions.count(d->value) || d->type == "FuncDecl") functions[d->value] = d;
                if (classes.count(d->value)) error(d, "'" + d->value + "' is already a class");
            } else if (d->type == "MethodDef") {
                const string& cls = d->at(3)->value;
                if (!classes.count(cls)) { error(d, "method definition for unknown class '" + cls + "'"); continue; }
                if (classes[cls].methods.count(d->value)) error(d, "method " + cls + "::" + d->value + " is already defined");
                else if (!has_proto(cls, d->value)) error(d, "method " + cls + "::" + d->value + " is not declared in the class");
                classes[cls].methods[d->value] = d;
            }
        }
        for (auto& kv : classes)
            for (const string& p : kv.second.protos)
                if (!kv.second.methods.count(p)) error(nullptr, "method " + kv.first + "::" + p + " is declared but never defined");
    }

    string check_args(Node* params, Node* args, const string& what, Node* at) {
        if (params->kids.size() != args->kids.size()) {
            error(at, what + " expects " + std::to_string(params->kids.size()) + " argument(s), got " + std::to_string(args->kids.size()));
            for (Node* a : args->kids) expr(a);
            return "";
        }
        for (size_t i = 0; i < args->kids.size(); ++i) {
            string at_ = expr(args->kids[i]);
            const string& pt = params->kids[i]->at(0)->value;
            if (!compatible(pt, at_)) error(args->kids[i], what + ": argument " + std::to_string(i + 1) + " has type " + at_ + ", expected " + pt);
        }
        return "ok";
    }

    bool is_lvalue(Node* n) { return n->type == "Ident" || n->type == "Member" || n->type == "Index"; }

    string expr(Node* n) {
        const string& t = n->type;
        if (t == "IntLit") return "int";
        if (t == "DoubleLit") return "double";
        if (t == "StringLit") return "string";
        if (t == "CharLit") return "char";
        if (t == "BoolLit") return "bool";
        if (t == "Ident") {
            string ty = lookup(n->value);
            if (ty.empty()) {
                if (functions.count(n->value)) error(n, "'" + n->value + "' is a function, not a variable");
                else error(n, "undeclared variable '" + n->value + "'");
            }
            return ty;
        }
        if (t == "This") {
            if (curClass.empty()) error(n, "'this' used outside of a method");
            return curClass;
        }
        if (t == "Member") {
            string ot = expr(n->at(0));
            if (ot.empty()) return "";
            if (!is_class(ot)) { error(n, "member access '." + n->value + "' on non-object of type " + ot); return ""; }
            string ft = field_type(ot, n->value);
            if (ft.empty()) error(n, "class " + ot + " has no field '" + n->value + "'");
            return ft;
        }
        if (t == "Index") {
            string at_ = expr(n->at(0)), it = expr(n->at(1));
            if (!at_.empty() && !is_array(at_)) error(n, "indexing a non-array of type " + at_);
            if (!it.empty() && it != "int" && it != "char" && it != "bool") error(n, "array index must be an integer, got " + it);
            return elem(at_);
        }
        if (t == "BinOp") {
            string a = expr(n->at(0)), b = expr(n->at(1));
            const string& op = n->value;
            if (a.empty() || b.empty()) return (op == "&&" || op == "||" || op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") ? "bool" : "";
            if (op == "&&" || op == "||") {
                if (a == "void" || b == "void" || is_array(a) || is_array(b) || is_class(a) || is_class(b)) error(n, "invalid operands of types " + a + " and " + b + " to '" + op + "'");
                return "bool";
            }
            bool cmp = op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=";
            if ((a == "string" || b == "string")) {
                bool ok = (a == "string" || a == "char") && (b == "string" || b == "char") && (op == "+" || cmp);
                if (!ok) error(n, "invalid operands of types " + a + " and " + b + " to '" + op + "'");
                return cmp ? "bool" : "string";
            }
            if (!is_numeric(a) || !is_numeric(b)) { error(n, "invalid operands of types " + a + " and " + b + " to '" + op + "'"); return ""; }
            if (cmp) return "bool";
            if (op == "%" && (a == "double" || b == "double")) error(n, "operator % cannot be applied to double");
            return (a == "double" || b == "double") ? "double" : "int";
        }
        if (t == "UnOp") {
            string a = expr(n->at(0));
            if (n->value == "!") { if (a == "void" || is_array(a) || is_class(a)) error(n, "invalid operand of type " + a + " to '!'"); return "bool"; }
            if (!a.empty() && !is_numeric(a)) error(n, "invalid operand of type " + a + " to unary '-'");
            return a == "double" ? "double" : "int";
        }
        if (t == "Cast") {
            string a = expr(n->at(0));
            if (!a.empty() && !is_numeric(a)) error(n, "cannot cast " + a + " to " + n->value);
            return n->value;
        }
        if (t == "Ternary") {
            string c = expr(n->at(0)), a = expr(n->at(1)), b = expr(n->at(2));
            if (c == "void" || is_array(c) || is_class(c)) error(n, "condition of type " + c + " in ?:");
            if (!compatible(a, b) && !compatible(b, a)) error(n, "incompatible branches of ?: " + a + " and " + b);
            if (a == "double" || b == "double") return "double";
            return a.empty() ? b : a;
        }
        if (t == "Assign") {
            Node* lhs = n->at(0);
            if (!is_lvalue(lhs)) { error(n, "left side of assignment is not assignable"); expr(n->at(1)); return ""; }
            string lt = expr(lhs), rt = expr(n->at(1));
            if (is_array(lt)) error(n, "cannot assign to an array");
            else if (n->value == "=") { if (!compatible(lt, rt)) error(n, "cannot assign " + rt + " to " + lt); }
            else {
                string op = n->value.substr(0, 1);
                bool ok = (is_numeric(lt) && is_numeric(rt)) || (lt == "string" && (rt == "string" || rt == "char") && op == "+");
                if (!lt.empty() && !rt.empty() && !ok) error(n, "invalid operands of types " + lt + " and " + rt + " to '" + n->value + "'");
            }
            return lt;
        }
        if (t == "IncDec") {
            Node* lhs = n->at(0);
            if (!is_lvalue(lhs)) { error(n, "operand of ++/-- is not assignable"); return ""; }
            string lt = expr(lhs);
            if (!lt.empty() && !is_numeric(lt)) error(n, "++/-- applied to " + lt);
            return lt;
        }
        if (t == "Call") {
            const string& f = n->value;
            Node* args = n->at(0);
            if (is_class(f)) {
                for (Node* a : args->kids) expr(a);
                check_ctor(f, args->kids.size(), n);
                return f;
            }
            auto fn = functions.find(f);
            if (fn != functions.end()) {
                check_args(fn->second->at(1), args, "function '" + f + "'", n);
                return fn->second->at(0)->value;
            }
            if (!curClass.empty()) {
                Node* m = find_method(curClass, f);
                if (m) { check_args(m->at(1), args, "method '" + f + "'", n); return m->at(0)->value; }
            }
            std::vector<string> types;
            for (Node* a : args->kids) types.push_back(expr(a));
            auto need = [&](size_t k) { if (types.size() != k) error(n, "'" + f + "' expects " + std::to_string(k) + " argument(s)"); };
            auto numeric_all = [&]() { for (auto& ty : types) if (!ty.empty() && !is_numeric(ty)) error(n, "'" + f + "' expects numeric arguments"); };
            if (f == "sqrt" || f == "abs") { need(1); numeric_all(); return f == "abs" && !types.empty() && types[0] != "double" ? "int" : "double"; }
            if (f == "pow") { need(2); numeric_all(); return "double"; }
            if (f == "max" || f == "min") { need(2); numeric_all(); return (!types.empty() && (types[0] == "double" || (types.size() > 1 && types[1] == "double"))) ? "double" : "int"; }
            if (f == "to_string") { need(1); return "string"; }
            error(n, "call to undeclared function '" + f + "'");
            return "";
        }
        if (t == "MethodCall") {
            string ot = expr(n->at(0));
            Node* args = n->at(1);
            if (ot == "string") {
                for (Node* a : args->kids) expr(a);
                if (n->value == "length" || n->value == "size") { if (!args->kids.empty()) error(n, n->value + "() takes no arguments"); return "int"; }
                error(n, "string has no method '" + n->value + "'");
                return "";
            }
            if (ot.empty()) { for (Node* a : args->kids) expr(a); return ""; }
            if (!is_class(ot)) { error(n, "method call '." + n->value + "()' on non-object of type " + ot); for (Node* a : args->kids) expr(a); return ""; }
            Node* m = find_method(ot, n->value);
            if (!m) { error(n, "class " + ot + " has no method '" + n->value + "'"); for (Node* a : args->kids) expr(a); return ""; }
            check_args(m->at(1), args, "method " + ot + "::" + n->value, n);
            return m->at(0)->value;
        }
        error(n, "unexpected expression node " + t);
        return "";
    }

    void check_ctor(const string& cls, size_t argc, Node* at) {
        ClassInfo& ci = classes[cls];
        if (ci.ctorArity.empty()) { if (argc) error(at, "class " + cls + " has no constructor taking " + std::to_string(argc) + " argument(s)"); return; }
        for (size_t a : ci.ctorArity) if (a == argc) return;
        error(at, "no matching constructor for " + cls + " with " + std::to_string(argc) + " argument(s)");
    }

    void condition(Node* c, const string& what) {
        string t = expr(c);
        if (t == "void" || is_array(t) || is_class(t)) error(c, what + " condition has type " + t);
    }

    void var_decl(Node* n, bool global) {
        if (n->type == "Decls") { for (Node* d : n->kids) var_decl(d, global); return; }
        const string& type = n->at(0)->value;
        if (!type_exists(type)) { error(n, "unknown type '" + type + "'"); }
        else if (type == "void") error(n, "variable '" + n->value + "' declared void");
        string full = type;
        if (n->type == "VarDecl") {
            if (n->at(1)) {
                string it = expr(n->at(1));
                if (!compatible(type, it)) error(n, "cannot initialize " + type + " '" + n->value + "' with " + it);
            }
        } else if (n->type == "ObjDecl") {
            if (!is_class(type)) error(n, "'" + type + "' is not a class");
            for (Node* a : n->at(1)->kids) expr(a);
            if (is_class(type)) check_ctor(type, n->at(1)->kids.size(), n);
        } else if (n->type == "ArrayDecl") {
            full = type + "[]";
            if (n->at(1)->type != "Empty") {
                string st = expr(n->at(1));
                if (!st.empty() && st != "int" && st != "char") error(n, "array size must be an integer, got " + st);
            }
            if (n->at(2)) for (Node* e : n->at(2)->kids) {
                string et = expr(e);
                if (!compatible(type, et)) error(e, "array element of type " + et + " in array of " + type);
            }
        }
        if (global) { if (globals.count(n->value)) error(n, "redeclaration of global '" + n->value + "'"); globals[n->value] = full; }
        else declare(n, n->value, full);
    }

    bool definitely_returns(Node* n) {
        const string& t = n->type;
        if (t == "Return") return true;
        if (t == "Block") { for (Node* s : n->kids) if (definitely_returns(s)) return true; return false; }
        if (t == "If") return n->at(2) && definitely_returns(n->at(1)) && definitely_returns(n->at(2));
        if (t == "While") return n->at(0)->type == "BoolLit" && n->at(0)->value == "true" && !has_break(n->at(1));
        if (t == "For") return n->at(1)->type == "Empty" && !has_break(n->at(3));
        if (t == "DoWhile") return definitely_returns(n->at(0));
        return false;
    }
    bool has_break(Node* n) {
        if (n->type == "Break") return true;
        if (n->type == "While" || n->type == "For" || n->type == "DoWhile") return false;
        for (Node* k : n->kids) if (has_break(k)) return true;
        return false;
    }

    void stmt(Node* n) {
        const string& t = n->type;
        if (t == "Block") { push(); for (Node* s : n->kids) stmt(s); pop(); }
        else if (t == "Empty" || t == "FuncProto") {}
        else if (t == "ExprStmt") expr(n->at(0));
        else if (t == "VarDecl" || t == "ObjDecl" || t == "ArrayDecl" || t == "Decls") var_decl(n, false);
        else if (t == "If") { condition(n->at(0), "if"); push(); stmt(n->at(1)); pop(); if (n->at(2)) { push(); stmt(n->at(2)); pop(); } }
        else if (t == "While") { condition(n->at(0), "while"); loopDepth++; push(); stmt(n->at(1)); pop(); loopDepth--; }
        else if (t == "DoWhile") { loopDepth++; push(); stmt(n->at(0)); pop(); loopDepth--; condition(n->at(1), "do-while"); }
        else if (t == "For") {
            push();
            stmt(n->at(0));
            if (n->at(1)->type != "Empty") condition(n->at(1), "for");
            loopDepth++; push(); stmt(n->at(3)); pop(); loopDepth--;
            stmt(n->at(2));
            pop();
        }
        else if (t == "Return") {
            if (!n->at(0)) { if (curReturn != "void") error(n, "function '" + curFunc + "' must return " + curReturn); }
            else {
                string rt = expr(n->at(0));
                if (curReturn == "void") error(n, "void function '" + curFunc + "' cannot return a value");
                else if (!compatible(curReturn, rt)) error(n, "function '" + curFunc + "' returns " + rt + ", expected " + curReturn);
            }
        }
        else if (t == "Break") { if (!loopDepth) error(n, "'break' outside of a loop"); }
        else if (t == "Continue") { if (!loopDepth) error(n, "'continue' outside of a loop"); }
        else if (t == "Cout") {
            for (Node* k : n->kids) {
                if (k->type == "Endl") continue;
                string ty = expr(k);
                if (ty == "void" || is_array(ty) || is_class(ty)) error(k, "cannot print a value of type " + ty);
            }
        }
        else if (t == "Cin") {
            for (Node* k : n->kids) {
                if (!is_lvalue(k)) { error(k, "cin target is not assignable"); continue; }
                string ty = expr(k);
                if (!ty.empty() && !is_numeric(ty) && ty != "string") error(k, "cannot read a value of type " + ty + " with cin");
            }
        }
        else error(n, "unexpected statement node " + t);
    }

    void function_body(Node* params, Node* body, const string& ret, const string& name) {
        inFunction = true; curReturn = ret; curFunc = name; loopDepth = 0;
        scopes.clear(); push();
        for (Node* p : params->kids) {
            const string& pt = p->at(0)->value;
            string base = is_array(pt) ? elem(pt) : pt;
            if (!type_exists(base) || base == "void") error(p, "parameter '" + p->value + "' has unknown type '" + pt + "'");
            if (scopes.back().count(p->value)) error(p, "duplicate parameter '" + p->value + "'");
            scopes.back()[p->value] = pt;
        }
        for (Node* s : body->kids) stmt(s);
        if (ret != "void" && name != "main" && !definitely_returns(body))
            error(body, "function '" + name + "' can reach the end without returning a value");
        pop(); inFunction = false;
    }

    void run(Node* prog) {
        collect(prog);
        scopes.clear(); push();
        for (Node* d : prog->kids)
            if (d->type == "VarDecl" || d->type == "ObjDecl" || d->type == "ArrayDecl" || d->type == "Decls") { curClass.clear(); var_decl(d, true); }

        for (Node* d : prog->kids) {
            if (d->type == "ClassDecl") {
                curClass = d->value;
                ClassInfo& ci = classes[d->value];
                for (Node* m : d->kids) {
                    if (m->type == "Field") {
                        const string& ft = m->at(0)->value;
                        if (!type_exists(ft) || ft == "void") error(m, "field '" + m->value + "' has unknown type '" + ft + "'");
                        if (m->at(1) && m->at(1)->type == "Dim") { scopes.clear(); push(); expr(m->at(1)->at(0)); }
                        else if (m->at(1)) { scopes.clear(); push(); string it = expr(m->at(1)); if (!compatible(ft, it)) error(m, "cannot initialize field '" + m->value + "' of type " + ft + " with " + it); }
                    } else if (m->type == "Method") {
                        if (!type_exists(m->at(0)->value)) error(m, "unknown return type '" + m->at(0)->value + "'");
                        function_body(m->at(1), m->at(2), m->at(0)->value, d->value + "::" + m->value);
                    } else if (m->type == "Ctor") {
                        inFunction = true; curReturn = "void"; curFunc = d->value + "::" + d->value; loopDepth = 0;
                        scopes.clear(); push();
                        for (Node* p : m->at(0)->kids) {
                            if (scopes.back().count(p->value)) error(p, "duplicate parameter '" + p->value + "'");
                            scopes.back()[p->value] = p->at(0)->value;
                        }
                        for (Node* init : m->at(1)->kids) {
                            if (init->value == ci.base) { for (Node* a : init->at(0)->kids) expr(a); check_ctor(ci.base, init->at(0)->kids.size(), init); }
                            else {
                                string ft = field_type(d->value, init->value);
                                if (ft.empty()) error(init, "initializer for unknown field '" + init->value + "'");
                                if (init->at(0)->kids.size() != 1) error(init, "field initializer '" + init->value + "' expects exactly 1 argument");
                                for (Node* a : init->at(0)->kids) { string at_ = expr(a); if (!compatible(ft, at_)) error(a, "cannot initialize field '" + init->value + "' of type " + ft + " with " + at_); }
                            }
                        }
                        for (Node* s : m->at(2)->kids) stmt(s);
                        pop(); inFunction = false;
                    }
                }
                curClass.clear();
            } else if (d->type == "MethodDef") {
                const string& cls = d->at(3)->value;
                if (!classes.count(cls)) continue;
                curClass = cls;
                function_body(d->at(1), d->at(2), d->at(0)->value, cls + "::" + d->value);
                curClass.clear();
            } else if (d->type == "FuncDecl") {
                curClass.clear();
                if (!type_exists(d->at(0)->value)) error(d, "unknown return type '" + d->at(0)->value + "'");
                function_body(d->at(1), d->at(2), d->at(0)->value, d->value);
            }
        }
        auto m = functions.find("main");
        if (m == functions.end() || m->second->type != "FuncDecl") error(nullptr, "no main() function");
        else {
            if (m->second->at(0)->value != "int") error(m->second, "main() must return int");
            if (!m->second->at(1)->kids.empty()) error(m->second, "main() must not take parameters");
        }
    }
};

} // namespace

std::vector<std::string> check_program(Node* program) {
    Sema s;
    s.run(program);
    return s.errors;
}
