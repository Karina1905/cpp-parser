#include "ast.hpp"
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using std::string;

namespace {

[[noreturn]] void cg_error(const string& msg, const Node* n = nullptr) {
    throw std::runtime_error("Codegen error" + (n && n->line ? " (line " + std::to_string(n->line) + ")" : "") + ": " + msg);
}

string py_str(const string& s) {
    string r = "\"";
    for (char c : s) {
        switch (c) {
            case '"':  r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n";  break;
            case '\t': r += "\\t";  break;
            case '\r': r += "\\r";  break;
            default:   r += c;
        }
    }
    return r + "\"";
}

struct VarInfo { string py; string type; };

struct ClassMeta {
    string base;
    std::vector<Node*> fields, ctors, methods;
    std::map<string, string> fieldTypes;
};

struct Gen {
    std::ostringstream out;
    int ind = 0;

    std::map<string, ClassMeta> classes;
    std::map<string, string> globals;
    std::map<string, Node*> functions;

    std::vector<std::map<string, VarInfo>> scopes;
    std::map<string, int> nameUse;
    std::set<string> fields, methods;
    string curClass;
    bool inMethod = false;
    std::set<string> usedGlobals;
    std::vector<Node*> loopSteps;

    void line(const string& s) { if (s.empty()) { out << "\n"; return; } out << string(ind * 4, ' ') << s << "\n"; }

    void collect_meta(Node* prog) {
        for (Node* d : prog->kids) {
            if (d->type == "ClassDecl") {
                ClassMeta m;
                for (Node* k : d->kids) {
                    if (k->type == "Base") m.base = k->value;
                    else if (k->type == "Field") { m.fields.push_back(k); m.fieldTypes[k->value] = k->at(0)->value + (k->at(1) && k->at(1)->type == "Dim" ? "[]" : ""); }
                    else if (k->type == "Ctor") m.ctors.push_back(k);
                    else if (k->type == "Method") m.methods.push_back(k);
                }
                classes[d->value] = m;
            }
        }
        for (Node* d : prog->kids) {
            if (d->type == "MethodDef") {
                auto it = classes.find(d->at(3)->value);
                if (it == classes.end()) cg_error("method for unknown class " + d->at(3)->value, d);
                it->second.methods.push_back(d);
            }
            if (d->type == "FuncDecl") functions[d->value] = d;
            if (d->type == "VarDecl" || d->type == "ObjDecl") globals[d->value] = d->at(0)->value;
            if (d->type == "ArrayDecl") globals[d->value] = d->at(0)->value + "[]";
            if (d->type == "Decls") for (Node* k : d->kids) globals[k->value] = k->at(0)->value + (k->type == "ArrayDecl" ? "[]" : "");
        }
    }
    bool is_class(const string& n) const { return classes.count(n) > 0; }

    void class_members(const string& cls) {
        fields.clear(); methods.clear();
        for (string c = cls; !c.empty(); c = classes[c].base) {
            if (!classes.count(c)) break;
            for (Node* x : classes[c].fields)  fields.insert(x->value);
            for (Node* x : classes[c].methods) methods.insert(x->value);
        }
    }
    string field_type(const string& cls, const string& name) {
        for (string c = cls; !c.empty(); c = classes[c].base) {
            if (!classes.count(c)) break;
            auto f = classes[c].fieldTypes.find(name);
            if (f != classes[c].fieldTypes.end()) return f->second;
        }
        return "";
    }

    void push_scope() { scopes.emplace_back(); }
    void pop_scope() { scopes.pop_back(); }
    VarInfo* resolve(const string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(name);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }
    string declare_local(const string& name, const string& type) {
        int n = ++nameUse[name];
        string py = name;
        if (n > 1) {
            bool shadows = false;
            for (auto& sc : scopes) if (sc.count(name)) shadows = true;
            if (shadows) py = name + "_" + std::to_string(n);
        }
        scopes.back()[name] = {py, type};
        return py;
    }

    string default_for(const string& t, Node* at) {
        if (t == "int") return "0";
        if (t == "double") return "0.0";
        if (t == "bool") return "False";
        if (t == "char") return "\"\\0\"";
        if (t == "string") return "\"\"";
        if (is_class(t)) return t + "()";
        cg_error("unknown type '" + t + "'", at);
    }

    string cast_to(const string& type, Node* init, const string& e) {
        const string& it = init ? init->type : "";
        if (type == "int")    return it == "IntLit" || it == "BoolLit" ? e : "_int(" + e + ")";
        if (type == "double") return it == "DoubleLit" ? e : "_float(" + e + ")";
        if (type == "bool")   return it == "BoolLit" ? e : "bool(_ord(" + e + "))";
        if (type == "char")   return it == "CharLit" ? e : "_chr(" + e + ")";
        if (is_class(type) && it != "Call" && it != "ObjDecl") return "copy.deepcopy(" + e + ")";
        return e;
    }

    string type_of(Node* n) {
        if (n->type == "Ident") {
            if (VarInfo* v = resolve(n->value)) return v->type;
            if (inMethod && fields.count(n->value)) return field_type(curClass, n->value);
            auto g = globals.find(n->value);
            if (g != globals.end()) return g->second;
            return "";
        }
        if (n->type == "This") return curClass;
        if (n->type == "Member") {
            string ot = type_of(n->at(0));
            return is_class(ot) ? field_type(ot, n->value) : "";
        }
        if (n->type == "Index") {
            string at = type_of(n->at(0));
            return at.size() > 2 && at.substr(at.size() - 2) == "[]" ? at.substr(0, at.size() - 2) : "";
        }
        if (n->type == "CharLit") return "char";
        if (n->type == "StringLit") return "string";
        if (n->type == "IntLit") return "int";
        if (n->type == "DoubleLit") return "double";
        if (n->type == "BoolLit") return "bool";
        if (n->type == "Cast") return n->value;
        if (n->type == "BinOp") {
            const string& o = n->value;
            if (o == "&&" || o == "||" || o == "==" || o == "!=" || o == "<" || o == ">" || o == "<=" || o == ">=") return "bool";
            string a = type_of(n->at(0)), b = type_of(n->at(1));
            if (a == "string" || b == "string") return "string";
            if (a == "double" || b == "double") return "double";
            return "int";
        }
        if (n->type == "Call") { auto f = functions.find(n->value); if (f != functions.end()) return f->second->at(0)->value; return ""; }
        return "";
    }
    string char_aware(Node* child, int parentPrec, bool right, bool otherIsString) {
        string s = sub(child, parentPrec, right);
        if (!otherIsString && type_of(child) == "char") return "_ord(" + s + ")";
        return s;
    }

    int prec(Node* n) {
        const string& t = n->type;
        if (t == "Ternary") return 1;
        if (t == "BinOp") {
            const string& o = n->value;
            if (o == "||") return 2;
            if (o == "&&") return 3;
            if (o == "==" || o == "!=" || o == "<" || o == ">" || o == "<=" || o == ">=") return 5;
            if (o == "+" || o == "-") return 6;
            if (o == "*") return 7;
            return 10;
        }
        if (t == "UnOp") return n->value == "!" ? 4 : 8;
        return 10;
    }
    string sub(Node* child, int parentPrec, bool right = false) {
        string s = expr(child);
        int p = prec(child);
        if (p < parentPrec || (right && p == parentPrec) || (p == 5 && parentPrec == 5)) return "(" + s + ")";
        return s;
    }

    string ident(Node* n) {
        const string& name = n->value;
        if (VarInfo* v = resolve(name)) return v->py;
        if (inMethod && fields.count(name)) return "self." + name;
        return name;
    }

    string args(Node* a) {
        string r;
        for (size_t i = 0; i < a->kids.size(); ++i) { if (i) r += ", "; r += expr(a->kids[i]); }
        return r;
    }

    string target(Node* lhs) {
        if (lhs->type == "Ident" && !resolve(lhs->value) && !(inMethod && fields.count(lhs->value)) && globals.count(lhs->value))
            usedGlobals.insert(lhs->value);
        return expr(lhs);
    }

    string compound(const string& lhs, const string& op, Node* rhsNode, const string& rhs, const string& type) {
        if (op == "/") return cast_to(type, nullptr, "_div(" + lhs + ", " + rhs + ")");
        if (op == "%") return cast_to(type, nullptr, "_mod(" + lhs + ", " + rhs + ")");
        string r = type_of(rhsNode) == "char" ? "_ord(" + rhs + ")" : rhs;
        string l = type == "char" ? "_ord(" + lhs + ")" : lhs;
        string e = l + " " + op + " " + r;
        if (type == "char") return "_chr(" + e + ")";
        if (type == "int" && rhsNode->type != "IntLit") return "int(" + e + ")";
        if (type == "double" || type == "int" || type.empty()) return e;
        return cast_to(type, nullptr, e);
    }

    string expr(Node* n) {
        const string& t = n->type;
        if (t == "IntLit")    return n->value;
        if (t == "DoubleLit") { string v = n->value; if (v.back() == '.') v += "0"; return v; }
        if (t == "StringLit") return py_str(n->value);
        if (t == "CharLit")   return py_str(n->value);
        if (t == "BoolLit")   return n->value == "true" ? "True" : "False";
        if (t == "Ident")     return ident(n);
        if (t == "This")      return "self";
        if (t == "Member")    return expr(n->at(0)) + "." + n->value;
        if (t == "Index")     return expr(n->at(0)) + "[" + expr(n->at(1)) + "]";

        if (t == "BinOp") {
            const string& o = n->value;
            string ta = type_of(n->at(0)), tb = type_of(n->at(1));
            bool str = ta == "string" || tb == "string";
            bool bothChar = ta == "char" && tb == "char";
            bool logical = o == "&&" || o == "||";
            bool keepChars = str || (bothChar && !logical && o != "+" && o != "-" && o != "*" && o != "/" && o != "%");
            if (o == "/") return "_div(" + char_aware(n->at(0), 0, false, keepChars) + ", " + char_aware(n->at(1), 0, false, keepChars) + ")";
            if (o == "%") return "_mod(" + char_aware(n->at(0), 0, false, keepChars) + ", " + char_aware(n->at(1), 0, false, keepChars) + ")";
            string op = o == "&&" ? "and" : o == "||" ? "or" : o;
            int p = prec(n);
            return char_aware(n->at(0), p, false, keepChars) + " " + op + " " + char_aware(n->at(1), p, true, keepChars);
        }
        if (t == "UnOp") {
            if (n->value == "!") return "not " + sub(n->at(0), 4);
            return "-" + char_aware(n->at(0), 8, false, false);
        }
        if (t == "Cast") {
            string e = expr(n->at(0));
            if (n->value == "int") return "_int(" + e + ")";
            if (n->value == "double") return "_float(" + e + ")";
            if (n->value == "bool") return "bool(_ord(" + e + "))";
            return "_chr(" + e + ")";
        }
        if (t == "Ternary") return sub(n->at(1), 1) + " if " + sub(n->at(0), 1) + " else " + sub(n->at(2), 1);

        if (t == "Assign" || t == "IncDec") {
            Node* lhs = n->at(0);
            string tg = target(lhs), type = type_of(lhs);
            bool simple = lhs->type == "Ident" && (resolve(lhs->value) || !(inMethod && fields.count(lhs->value)));
            if (!simple) {
                string obj, key, kind;
                if (lhs->type == "Index") { kind = "item"; obj = expr(lhs->at(0)); key = expr(lhs->at(1)); }
                else if (lhs->type == "Member") { kind = "attr"; obj = expr(lhs->at(0)); key = py_str(lhs->value); }
                else { kind = "attr"; obj = "self"; key = py_str(lhs->value); }
                if (t == "Assign") {
                    string rhs = expr(n->at(1));
                    string val = n->value == "=" ? cast_to(type, n->at(1), rhs) : compound(tg, n->value.substr(0, 1), n->at(1), rhs, type);
                    return "_set" + kind + "(" + obj + ", " + key + ", " + val + ")";
                }
                bool inc = n->value.find("++") != string::npos;
                bool post = n->value.rfind("post", 0) == 0;
                return "_inc" + kind + "(" + obj + ", " + key + ", " + (inc ? "1" : "-1") + ", " + (post ? "True" : "False") + (type == "char" ? ", True" : "") + ")";
            }
            if (t == "Assign") {
                string rhs = expr(n->at(1));
                string val = n->value == "=" ? cast_to(type, n->at(1), rhs) : compound(tg, n->value.substr(0, 1), n->at(1), rhs, type);
                return "(" + tg + " := " + val + ")";
            }
            bool inc = n->value.find("++") != string::npos;
            string w = type == "char" ? "(" + tg + " := _chr(_ord(" + tg + ")" + (inc ? " + 1))" : " - 1))")
                                      : "(" + tg + " := " + tg + (inc ? " + 1" : " - 1") + ")";
            return n->value.rfind("post", 0) == 0 ? "(" + w + (inc ? " - 1" : " + 1") + ")" : w;
        }

        if (t == "Call") {
            const string& f = n->value;
            string a = args(n->at(0));
            if (is_class(f)) return f + "(" + a + ")";
            if (inMethod && methods.count(f) && !functions.count(f)) return "self." + f + "(" + a + ")";
            if (f == "sqrt") return "math.sqrt(" + a + ")";
            if (f == "pow")  return "math.pow(" + a + ")";
            if (f == "to_string") return "str(" + a + ")";
            return f + "(" + a + ")";
        }
        if (t == "MethodCall") {
            if ((n->value == "length" || n->value == "size") && !is_class(type_of(n->at(0)))) return "len(" + expr(n->at(0)) + ")";
            return expr(n->at(0)) + "." + n->value + "(" + args(n->at(1)) + ")";
        }
        cg_error("cannot translate expression node " + t, n);
    }

    void assign_stmt(Node* e) {
        Node* rhsNode = e->at(1);
        if (e->value == "=" && rhsNode->type == "Assign" && rhsNode->value == "=") {
            assign_stmt(rhsNode);
            Node* chainTarget = rhsNode->at(0);
            string lhs = target(e->at(0));
            line(lhs + " = " + cast_to(type_of(e->at(0)), chainTarget, expr(chainTarget)));
            return;
        }
        string lhs = target(e->at(0)), rhs = expr(rhsNode), type = type_of(e->at(0));
        const string& op = e->value;
        if (op == "=") line(lhs + " = " + cast_to(type, rhsNode, rhs));
        else if (op == "/=" || op == "%=" || type == "int" || type == "bool" || type == "char")
            line(lhs + " = " + compound(lhs, op.substr(0, 1), rhsNode, rhs, type));
        else line(lhs + " " + op + " " + rhs);
    }

    void expr_stmt(Node* e) {
        if (e->type == "Assign") assign_stmt(e);
        else if (e->type == "IncDec") {
            string lhs = target(e->at(0));
            bool inc = e->value.find("++") != string::npos;
            if (type_of(e->at(0)) == "char") line(lhs + " = _chr(_ord(" + lhs + ")" + (inc ? " + 1)" : " - 1)"));
            else line(lhs + (inc ? " += 1" : " -= 1"));
        } else line(expr(e));
    }

    void declare(Node* n) {
        if (n->type == "Decls") { for (Node* d : n->kids) declare(d); return; }
        const string& type = n->at(0)->value;
        if (n->type == "VarDecl") {
            string init = n->at(1) ? cast_to(type, n->at(1), expr(n->at(1))) : default_for(type, n);
            line(declare_local(n->value, type) + " = " + init);
        } else if (n->type == "ObjDecl") {
            string init = type + "(" + args(n->at(1)) + ")";
            line(declare_local(n->value, type) + " = " + init);
        } else if (n->type == "ArrayDecl") {
            string def = default_for(type, n);
            Node* init = n->at(2);
            string sizeExpr = n->at(1)->type == "Empty" ? "" : expr(n->at(1));
            string items;
            if (init) for (size_t i = 0; i < init->kids.size(); ++i) { if (i) items += ", "; items += cast_to(type, init->kids[i], expr(init->kids[i])); }
            string py = declare_local(n->value, type + "[]");
            string fill = is_class(type) ? "[" + def + " for _ in range(" + sizeExpr + ")]" : "[" + def + "] * (" + sizeExpr + ")";
            if (!init) line(py + " = " + fill);
            else if (sizeExpr.empty()) line(py + " = [" + items + "]");
            else line(py + " = ([" + items + "] + " + fill + ")[:" + sizeExpr + "]");
        }
    }

    void body(Node* block) {
        ind++;
        size_t before = out.str().size();
        push_scope();
        if (block->type == "Block") for (Node* s : block->kids) stmt(s);
        else stmt(block);
        pop_scope();
        if (out.str().size() == before) line("pass");
        ind--;
    }

    void stmt(Node* n) {
        const string& t = n->type;
        if (t == "Block") { push_scope(); for (Node* s : n->kids) stmt(s); pop_scope(); }
        else if (t == "Empty" || t == "FuncProto") {}
        else if (t == "ExprStmt") expr_stmt(n->at(0));
        else if (t == "VarDecl" || t == "ObjDecl" || t == "ArrayDecl" || t == "Decls") declare(n);
        else if (t == "If") {
            line("if " + expr(n->at(0)) + ":");
            body(n->at(1));
            Node* els = n->at(2);
            while (els && els->type == "If") {
                line("elif " + expr(els->at(0)) + ":");
                body(els->at(1));
                els = els->at(2);
            }
            if (els) { line("else:"); body(els); }
        }
        else if (t == "While") {
            line("while " + expr(n->at(0)) + ":");
            loopSteps.push_back(nullptr); body(n->at(1)); loopSteps.pop_back();
        }
        else if (t == "DoWhile") {
            line("while True:");
            loopSteps.push_back(nullptr);
            ind++;
            push_scope(); stmt(n->at(0)); pop_scope();
            line("if not (" + expr(n->at(1)) + "):");
            ind++; line("break"); ind--;
            ind--;
            loopSteps.pop_back();
        }
        else if (t == "For") {
            push_scope();
            stmt(n->at(0));
            Node* cond = n->at(1);
            line("while " + (cond->type == "Empty" ? string("True") : expr(cond)) + ":");
            loopSteps.push_back(n->at(2));
            body(n->at(3));
            ind++; stmt(n->at(2)); ind--;
            loopSteps.pop_back();
            pop_scope();
        }
        else if (t == "Return") {
            if (!n->at(0)) line("return");
            else line("return " + cast_to(curReturnType, n->at(0), expr(n->at(0))));
        }
        else if (t == "Break") line("break");
        else if (t == "Continue") {
            if (!loopSteps.empty() && loopSteps.back()) stmt(loopSteps.back());
            line("continue");
        }
        else if (t == "Cout") {
            string items;
            for (Node* k : n->kids) {
                if (!items.empty()) items += ", ";
                if (k->type == "Endl") items += "\"\\n\"";
                else if (k->type == "StringLit") items += py_str(k->value);
                else items += "_fmt(" + expr(k) + ")";
            }
            line("print(" + items + ", sep=\"\", end=\"\")");
        }
        else if (t == "Cin") {
            for (Node* k : n->kids) { string tg = target(k); line(tg + " = _cin(" + tg + ")"); }
        }
        else cg_error("cannot translate statement node " + t, n);
    }

    string curReturnType;

    void function_body(Node* params, Node* block, const string& retType, const std::vector<string>& prologue = {}) {
        scopes.clear(); nameUse.clear(); usedGlobals.clear();
        curReturnType = retType;
        push_scope();
        std::vector<string> coerce;
        for (Node* p : params->kids) {
            const string& pt = p->at(0)->value;
            declare_local(p->value, pt);
            if (pt == "int") coerce.push_back(p->value + " = _int(" + p->value + ")");
            else if (pt == "double") coerce.push_back(p->value + " = _float(" + p->value + ")");
            else if (is_class(pt)) coerce.push_back(p->value + " = copy.deepcopy(" + p->value + ")");
        }

        std::ostringstream saved; saved.swap(out);
        int savedInd = ind; ind = savedInd + 1;
        for (auto& c : coerce) line(c);
        for (auto& pr : prologue) line(pr);
        bool empty = coerce.empty() && prologue.empty();
        if (block->type == "Block") { for (Node* s : block->kids) stmt(s); if (!block->kids.empty()) empty = false; }
        string bodyText = out.str();
        out.swap(saved); ind = savedInd;

        ind++;
        if (!usedGlobals.empty()) {
            string g; for (auto& s : usedGlobals) { if (!g.empty()) g += ", "; g += s; }
            line("global " + g);
        }
        ind--;
        out << bodyText;
        if (empty && usedGlobals.empty()) { ind++; line("pass"); ind--; }
        pop_scope();
    }

    string param_list(Node* params, bool self) {
        string r = self ? "self" : "";
        for (Node* p : params->kids) { if (!r.empty()) r += ", "; r += p->value; }
        return r;
    }

    void function(Node* f) {
        inMethod = false; curClass.clear(); fields.clear(); methods.clear();
        line("def " + f->value + "(" + param_list(f->at(1), false) + "):");
        function_body(f->at(1), f->at(2), f->at(0)->value);
        line("");
    }

    void ctor_body(const string& cls, Node* ctor) {
        ClassMeta& m = classes[cls];
        std::vector<string> pro;
        scopes.clear(); push_scope();
        Node* params = ctor ? ctor->at(0) : nullptr;
        if (ctor) for (Node* p : params->kids) declare_local(p->value, p->at(0)->value);
        for (Node* f : m.fields) {
            Node* extra = f->at(1);
            const string& ft = f->at(0)->value;
            if (extra && extra->type == "Dim") pro.push_back("self." + f->value + " = " + (is_class(ft) ? "[" + default_for(ft, f) + " for _ in range(" + expr(extra->at(0)) + ")]" : "[" + default_for(ft, f) + "] * (" + expr(extra->at(0)) + ")"));
            else if (extra) pro.push_back("self." + f->value + " = " + cast_to(ft, extra, expr(extra)));
            else pro.push_back("self." + f->value + " = " + default_for(ft, f));
        }
        bool baseCalled = false;
        std::vector<string> inits;
        if (ctor) {
            for (Node* init : ctor->at(1)->kids) {
                string a = args(init->at(0));
                if (init->value == m.base) { pro.push_back(m.base + ".__init__(self" + (a.empty() ? "" : ", " + a) + ")"); baseCalled = true; }
                else {
                    Node* v = init->at(0)->kids.empty() ? nullptr : init->at(0)->kids[0];
                    inits.push_back("self." + init->value + " = " + cast_to(field_type(cls, init->value), v, a));
                }
            }
        }
        if (!baseCalled && !m.base.empty()) pro.push_back(m.base + ".__init__(self)");
        for (auto& i : inits) pro.push_back(i);
        if (ctor) function_body(params, ctor->at(2), "void", pro);
        else { Node empty("Params"); Node blk("Block"); function_body(&empty, &blk, "void", pro); }
    }

    void class_decl(const string& cls) {
        ClassMeta& m = classes[cls];
        line("class " + cls + (m.base.empty() ? "" : "(" + m.base + ")") + ":");
        ind++;
        inMethod = true; curClass = cls;
        class_members(cls);

        if (m.ctors.size() <= 1) {
            Node* c = m.ctors.empty() ? nullptr : m.ctors[0];
            line("def __init__(" + (c ? param_list(c->at(0), true) : string("self")) + "):");
            ctor_body(cls, c);
        } else {
            line("def __init__(self, *args):");
            ind++;
            for (Node* c : m.ctors) {
                Node* ps = c->at(0);
                line((c == m.ctors[0] ? "if" : "elif") + string(" len(args) == ") + std::to_string(ps->kids.size()) + ":");
                ind++;
                if (!ps->kids.empty()) line(param_list(ps, false) + (ps->kids.size() == 1 ? ", = args" : " = args"));
                ind--;
                ctor_body(cls, c);
            }
            line("else:");
            ind++; line("raise TypeError(\"no matching constructor for " + cls + "\")"); ind--;
            ind--;
        }
        line("");
        for (Node* f : m.methods) {
            class_members(cls);
            line("def " + f->value + "(" + param_list(f->at(1), true) + "):");
            function_body(f->at(1), f->at(2), f->at(0)->value);
            line("");
        }
        inMethod = false; curClass.clear();
        ind--;
        line("");
    }

    string program(Node* prog) {
        collect_meta(prog);
        line("# generated by cppparser");
        line("import math, sys, os, copy, threading, traceback");
        line("");
        line("def _div(a, b):");
        line("    if isinstance(a, int) and isinstance(b, int):");
        line("        q = abs(a) // abs(b)");
        line("        return q if (a < 0) == (b < 0) else -q");
        line("    return a / b");
        line("");
        line("def _mod(a, b):");
        line("    return a - b * _div(a, b)");
        line("");
        line("def _chr(v):");
        line("    return chr(v) if isinstance(v, int) else v");
        line("");
        line("def _ord(v):");
        line("    return ord(v) if isinstance(v, str) else v");
        line("");
        line("def _int(v):");
        line("    return ord(v) if isinstance(v, str) else int(v)");
        line("");
        line("def _float(v):");
        line("    return float(ord(v)) if isinstance(v, str) else float(v)");
        line("");
        line("def _setattr(o, k, v):");
        line("    setattr(o, k, v)");
        line("    return v");
        line("");
        line("def _setitem(a, i, v):");
        line("    a[i] = v");
        line("    return v");
        line("");
        line("def _incattr(o, k, d, post, ch=False):");
        line("    old = getattr(o, k)");
        line("    new = _chr(_ord(old) + d) if ch else old + d");
        line("    setattr(o, k, new)");
        line("    return old if post else new");
        line("");
        line("def _incitem(a, i, d, post, ch=False):");
        line("    old = a[i]");
        line("    new = _chr(_ord(old) + d) if ch else old + d");
        line("    a[i] = new");
        line("    return old if post else new");
        line("");
        line("def _fmt(v):");
        line("    if isinstance(v, bool): return \"1\" if v else \"0\"");
        line("    if isinstance(v, float): return \"%g\" % v");
        line("    return str(v)");
        line("");
        line("_tokens = []");
        line("def _cin(cur):");
        line("    global _tokens");
        line("    while not _tokens:");
        line("        ln = sys.stdin.readline()");
        line("        if not ln: raise EOFError(\"cin: end of input\")");
        line("        _tokens = ln.split()");
        line("    tok = _tokens.pop(0)");
        line("    return float(tok) if isinstance(cur, float) else int(tok) if isinstance(cur, int) else tok");
        line("");
        for (Node* d : prog->kids) if (d->type == "ClassDecl") class_decl(d->value);
        for (Node* d : prog->kids) if (d->type == "FuncDecl") function(d);
        inMethod = false; scopes.clear(); push_scope(); nameUse.clear();
        for (Node* d : prog->kids)
            if (d->type == "VarDecl" || d->type == "ObjDecl" || d->type == "ArrayDecl" || d->type == "Decls") declare(d);
        pop_scope();
        line("");
        line("def _run():");
        line("    try:");
        line("        main()");
        line("    except BaseException:");
        line("        traceback.print_exc()");
        line("        sys.stdout.flush()");
        line("        os._exit(1)");
        line("");
        line("if __name__ == \"__main__\":");
        line("    sys.setrecursionlimit(1000000)");
        line("    threading.stack_size(512 * 1024 * 1024)");
        line("    _t = threading.Thread(target=_run)");
        line("    _t.start()");
        line("    _t.join()");
        return out.str();
    }
};

} // namespace

std::string gen_python(Node* program) {
    Gen g;
    return g.program(program);
}
