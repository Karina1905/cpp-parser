#include "ast.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <pthread.h>

using std::string;

struct Object;
struct Value {
    enum T { VOID, INT, DBL, BOOL, CHR, STR, OBJ, ARR } t = VOID;
    long long i = 0;
    double d = 0;
    string s;
    std::shared_ptr<Object> o;
    std::shared_ptr<std::vector<Value>> a;

    static Value Int(long long v)  { Value x; x.t = INT;  x.i = v; return x; }
    static Value Dbl(double v)     { Value x; x.t = DBL;  x.d = v; return x; }
    static Value Bool(bool v)      { Value x; x.t = BOOL; x.i = v; return x; }
    static Value Chr(char v)       { Value x; x.t = CHR;  x.i = (unsigned char)v; return x; }
    static Value Str(const string& v) { Value x; x.t = STR; x.s = v; return x; }

    bool isNum() const { return t == INT || t == DBL || t == BOOL || t == CHR; }
    double num() const { return t == DBL ? d : (double)i; }
    long long inum() const { return t == DBL ? (long long)d : i; }
    bool truthy() const {
        if (t == DBL) return d != 0;
        if (t == STR) return !s.empty();
        if (t == OBJ) return (bool)o;
        return i != 0;
    }
};

struct Object {
    string cls;
    std::map<string, Value> fields;
};

static string fmt_double(double d) {
    char buf[64]; snprintf(buf, sizeof buf, "%g", d); return buf;
}

static string show(const Value& v) {
    switch (v.t) {
        case Value::INT:  return std::to_string(v.i);
        case Value::DBL:  return fmt_double(v.d);
        case Value::BOOL: return v.i ? "1" : "0";
        case Value::CHR:  return string(1, (char)v.i);
        case Value::STR:  return v.s;
        case Value::OBJ:  return "<object " + (v.o ? v.o->cls : "null") + ">";
        case Value::ARR:  return "<array>";
        default:          return "<void>";
    }
}

[[noreturn]] static void rt_error(const string& msg, const Node* n = nullptr) {
    throw std::runtime_error("Runtime error" + (n && n->line ? " (line " + std::to_string(n->line) + ")" : "") + ": " + msg);
}

struct ClassInfo {
    string name, base;
    std::vector<Node*> fields;
    std::map<string, Node*> methods;
    std::vector<Node*> ctors;
};

static std::map<string, ClassInfo> classes;
static std::map<string, Node*> functions;
static std::map<string, Value> globals;

struct Env {
    std::vector<std::map<string, Value>> scopes;
    std::shared_ptr<Object> self;

    Env() { scopes.emplace_back(); }
    void push() { scopes.emplace_back(); }
    void pop()  { scopes.pop_back(); }
    void define(const string& n, const Value& v) { scopes.back()[n] = v; }

    Value* find(const string& n) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return &f->second;
        }
        if (self) { auto f = self->fields.find(n); if (f != self->fields.end()) return &f->second; }
        auto g = globals.find(n);
        if (g != globals.end()) return &g->second;
        return nullptr;
    }
};

struct BreakSignal {};
struct ContinueSignal {};
struct ReturnSignal { Value v; };

static Value eval(Node* n, Env& env);
static void  exec(Node* n, Env& env);
static Value construct(const string& cls, const std::vector<Value>& args, Node* at);
static Value call_user(Node* fn, const std::vector<Value>& args, std::shared_ptr<Object> self, Node* at);

static Value coerce(const Value& v, const string& type) {
    if (type == "int")    { if (v.t == Value::STR) rt_error("cannot convert string to int"); return Value::Int(v.inum()); }
    if (type == "double") { if (v.t == Value::STR) rt_error("cannot convert string to double"); return Value::Dbl(v.num()); }
    if (type == "bool")   return Value::Bool(v.truthy());
    if (type == "char")   { if (v.t == Value::STR && v.s.size() == 1) return Value::Chr(v.s[0]); return Value::Chr((char)v.inum()); }
    if (type == "string") { if (v.t == Value::CHR) return Value::Str(string(1, (char)v.i)); return v; }
    return v;
}

static Value deep_copy(const Value& v) {
    if (v.t == Value::OBJ && v.o) {
        Value r = v;
        r.o = std::make_shared<Object>(*v.o);
        for (auto& kv : r.o->fields) kv.second = deep_copy(kv.second);
        return r;
    }
    return v;
}

static Value default_value(Node* type, Node* at) {
    const string& t = type->value;
    if (t == "int")    return Value::Int(0);
    if (t == "double") return Value::Dbl(0);
    if (t == "bool")   return Value::Bool(false);
    if (t == "char")   return Value::Chr(0);
    if (t == "string") return Value::Str("");
    if (classes.count(t)) return construct(t, {}, at);
    rt_error("unknown type '" + t + "'", at);
}

static bool is_class_type(const string& t) { return classes.count(t) > 0; }

static Value* lvalue(Node* n, Env& env) {
    if (n->type == "Ident") {
        Value* v = env.find(n->value);
        if (!v) rt_error("undefined variable '" + n->value + "'", n);
        return v;
    }
    if (n->type == "Member") {
        Value obj = eval(n->at(0), env);
        if (obj.t != Value::OBJ || !obj.o) rt_error("member access on non-object", n);
        auto f = obj.o->fields.find(n->value);
        if (f == obj.o->fields.end()) rt_error("no field '" + n->value + "' in class " + obj.o->cls, n);
        return &f->second;
    }
    if (n->type == "Index") {
        Value arr = eval(n->at(0), env);
        Value idx = eval(n->at(1), env);
        if (arr.t != Value::ARR) rt_error("indexing a non-array", n);
        long long k = idx.inum();
        if (k < 0 || k >= (long long)arr.a->size()) rt_error("array index out of range: " + std::to_string(k), n);
        return &(*arr.a)[k];
    }
    rt_error("expression is not assignable", n);
}

static Value binop(const string& op, const Value& a, const Value& b, Node* at) {
    if (op == "&&") return Value::Bool(a.truthy() && b.truthy());
    if (op == "||") return Value::Bool(a.truthy() || b.truthy());

    if (a.t == Value::STR || b.t == Value::STR) {
        string x = show(a), y = show(b);
        if (op == "+")  return Value::Str(x + y);
        if (op == "==") return Value::Bool(x == y);
        if (op == "!=") return Value::Bool(x != y);
        if (op == "<")  return Value::Bool(x < y);
        if (op == ">")  return Value::Bool(x > y);
        if (op == "<=") return Value::Bool(x <= y);
        if (op == ">=") return Value::Bool(x >= y);
        rt_error("invalid operator '" + op + "' for strings", at);
    }
    if (!a.isNum() || !b.isNum()) rt_error("invalid operands for '" + op + "'", at);

    bool dbl = (a.t == Value::DBL || b.t == Value::DBL);
    if (dbl) {
        double x = a.num(), y = b.num();
        if (op == "+") return Value::Dbl(x + y);
        if (op == "-") return Value::Dbl(x - y);
        if (op == "*") return Value::Dbl(x * y);
        if (op == "/") return Value::Dbl(x / y);
        if (op == "%") rt_error("operator % on floating point", at);
        if (op == "==") return Value::Bool(x == y);
        if (op == "!=") return Value::Bool(x != y);
        if (op == "<")  return Value::Bool(x < y);
        if (op == ">")  return Value::Bool(x > y);
        if (op == "<=") return Value::Bool(x <= y);
        if (op == ">=") return Value::Bool(x >= y);
    } else {
        long long x = a.i, y = b.i;
        if (op == "+") return Value::Int(x + y);
        if (op == "-") return Value::Int(x - y);
        if (op == "*") return Value::Int(x * y);
        if (op == "/") { if (!y) rt_error("integer division by zero", at); return Value::Int(x / y); }
        if (op == "%") { if (!y) rt_error("modulo by zero", at); return Value::Int(x % y); }
        if (op == "==") return Value::Bool(x == y);
        if (op == "!=") return Value::Bool(x != y);
        if (op == "<")  return Value::Bool(x < y);
        if (op == ">")  return Value::Bool(x > y);
        if (op == "<=") return Value::Bool(x <= y);
        if (op == ">=") return Value::Bool(x >= y);
    }
    rt_error("unknown operator '" + op + "'", at);
}

static void assign_to(Value* dst, const Value& src) {
    switch (dst->t) {
        case Value::INT:  *dst = coerce(src, "int"); break;
        case Value::DBL:  *dst = coerce(src, "double"); break;
        case Value::BOOL: *dst = coerce(src, "bool"); break;
        case Value::CHR:  *dst = coerce(src, "char"); break;
        case Value::STR:  *dst = coerce(src, "string"); break;
        case Value::OBJ:  *dst = deep_copy(src); break;
        default:          *dst = src;
    }
}

static std::vector<Value> eval_args(Node* args, Env& env) {
    std::vector<Value> r;
    for (Node* k : args->kids) r.push_back(eval(k, env));
    return r;
}

static Node* find_method(const string& cls, const string& name) {
    string c = cls;
    while (!c.empty()) {
        auto ci = classes.find(c);
        if (ci == classes.end()) return nullptr;
        auto m = ci->second.methods.find(name);
        if (m != ci->second.methods.end()) return m->second;
        c = ci->second.base;
    }
    return nullptr;
}

static Value builtin(const string& name, const std::vector<Value>& a, Node* at, bool& found) {
    found = true;
    auto need = [&](size_t n) { if (a.size() != n) rt_error(name + " expects " + std::to_string(n) + " argument(s)", at); };
    if (name == "sqrt")  { need(1); return Value::Dbl(std::sqrt(a[0].num())); }
    if (name == "pow")   { need(2); return Value::Dbl(std::pow(a[0].num(), a[1].num())); }
    if (name == "abs")   { need(1); return a[0].t == Value::DBL ? Value::Dbl(std::fabs(a[0].d)) : Value::Int(std::llabs(a[0].i)); }
    if (name == "max")   { need(2); return binop(">", a[0], a[1], at).truthy() ? a[0] : a[1]; }
    if (name == "min")   { need(2); return binop("<", a[0], a[1], at).truthy() ? a[0] : a[1]; }
    if (name == "to_string") { need(1); return Value::Str(show(a[0])); }
    found = false;
    return Value();
}

static Value eval(Node* n, Env& env) {
    const string& t = n->type;

    if (t == "IntLit")    return Value::Int(std::stoll(n->value));
    if (t == "DoubleLit") return Value::Dbl(std::stod(n->value));
    if (t == "StringLit") return Value::Str(n->value);
    if (t == "CharLit")   return Value::Chr(n->value.empty() ? 0 : n->value[0]);
    if (t == "BoolLit")   return Value::Bool(n->value == "true");
    if (t == "Ident")     return *lvalue(n, env);
    if (t == "This") {
        if (!env.self) rt_error("'this' outside of a method", n);
        Value v; v.t = Value::OBJ; v.o = env.self; return v;
    }
    if (t == "Member" || t == "Index") return *lvalue(n, env);

    if (t == "BinOp") {
        Value a = eval(n->at(0), env);
        if (n->value == "&&") return Value::Bool(a.truthy() && eval(n->at(1), env).truthy());
        if (n->value == "||") return Value::Bool(a.truthy() || eval(n->at(1), env).truthy());
        Value b = eval(n->at(1), env);
        return binop(n->value, a, b, n);
    }
    if (t == "UnOp") {
        Value a = eval(n->at(0), env);
        if (n->value == "!") return Value::Bool(!a.truthy());
        if (a.t == Value::DBL) return Value::Dbl(-a.d);
        return Value::Int(-a.inum());
    }
    if (t == "Cast") return coerce(eval(n->at(0), env), n->value);
    if (t == "Ternary") return eval(n->at(0), env).truthy() ? eval(n->at(1), env) : eval(n->at(2), env);

    if (t == "Assign") {
        Value* dst = lvalue(n->at(0), env);
        Value rhs = eval(n->at(1), env);
        if (n->value == "=") assign_to(dst, rhs);
        else assign_to(dst, binop(n->value.substr(0, 1), *dst, rhs, n));
        return *dst;
    }
    if (t == "IncDec") {
        Value* dst = lvalue(n->at(0), env);
        Value old = *dst;
        bool inc = n->value.find("++") != string::npos;
        assign_to(dst, binop(inc ? "+" : "-", *dst, Value::Int(1), n));
        return n->value.rfind("post", 0) == 0 ? old : *dst;
    }

    if (t == "Call") {
        std::vector<Value> args = eval_args(n->at(0), env);
        if (is_class_type(n->value)) return construct(n->value, args, n);
        auto f = functions.find(n->value);
        if (f != functions.end()) return call_user(f->second, args, nullptr, n);
        if (env.self) {
            Node* m = find_method(env.self->cls, n->value);
            if (m) return call_user(m, args, env.self, n);
        }
        bool found; Value r = builtin(n->value, args, n, found);
        if (found) return r;
        rt_error("undefined function '" + n->value + "'", n);
    }
    if (t == "MethodCall") {
        Value obj = eval(n->at(0), env);
        std::vector<Value> args = eval_args(n->at(1), env);
        if (obj.t == Value::STR) {
            if (n->value == "length" || n->value == "size") return Value::Int((long long)obj.s.size());
            rt_error("unknown string method '" + n->value + "'", n);
        }
        if (obj.t != Value::OBJ || !obj.o) rt_error("method call on non-object", n);
        Node* m = find_method(obj.o->cls, n->value);
        if (!m) rt_error("class " + obj.o->cls + " has no method '" + n->value + "'", n);
        return call_user(m, args, obj.o, n);
    }
    rt_error("cannot evaluate node " + t, n);
}

static int call_depth = 0;
static const int MAX_DEPTH = 50000;

struct DepthGuard {
    DepthGuard(Node* at) { if (++call_depth > MAX_DEPTH) { --call_depth; rt_error("stack overflow: recursion deeper than " + std::to_string(MAX_DEPTH) + " calls", at); } }
    ~DepthGuard() { --call_depth; }
};

static Value call_user(Node* fn, const std::vector<Value>& args, std::shared_ptr<Object> self, Node* at) {
    DepthGuard guard(at);
    Node* params = fn->at(1);
    Node* body = fn->at(2);
    if (params->kids.size() != args.size())
        rt_error("function '" + fn->value + "' expects " + std::to_string(params->kids.size()) + " argument(s)", at);
    Env env;
    env.self = self;
    for (size_t i = 0; i < args.size(); ++i) {
        Node* p = params->kids[i];
        const string& pt = p->at(0)->value;
        Value v = (pt.size() > 2 && pt.substr(pt.size() - 2) == "[]") ? args[i] : coerce(args[i], pt);
        if (v.t == Value::OBJ) v = deep_copy(v);
        env.define(p->value, v);
    }
    const string& rtype = fn->at(0)->value;
    try { exec(body, env); }
    catch (ReturnSignal& r) {
        if (rtype == "void" && r.v.t != Value::VOID) rt_error("void function '" + fn->value + "' returns a value", at);
        if (rtype != "void" && r.v.t == Value::VOID) rt_error("function '" + fn->value + "' returns no value", at);
        return coerce(r.v, rtype);
    }
    if (rtype != "void") rt_error("function '" + fn->value + "' reached end without return", at);
    return Value();
}

static Value construct(const string& cls, const std::vector<Value>& args, Node* at) {
    auto ci = classes.find(cls);
    if (ci == classes.end()) rt_error("unknown class '" + cls + "'", at);
    ClassInfo& C = ci->second;

    auto obj = std::make_shared<Object>();
    obj->cls = cls;

    std::vector<ClassInfo*> chain;
    for (string c = cls; !c.empty(); c = classes[c].base) chain.push_back(&classes[c]);
    std::reverse(chain.begin(), chain.end());
    Env fenv; fenv.self = obj;
    for (ClassInfo* k : chain)
        for (Node* f : k->fields) {
            Node* extra = f->at(1);
            if (extra && extra->type == "Dim") {
                Value arr; arr.t = Value::ARR; arr.a = std::make_shared<std::vector<Value>>();
                long long sz = eval(extra->at(0), fenv).inum();
                for (long long i = 0; i < sz; ++i) arr.a->push_back(default_value(f->at(0), f));
                obj->fields[f->value] = arr;
            } else if (extra) obj->fields[f->value] = coerce(eval(extra, fenv), f->at(0)->value);
            else obj->fields[f->value] = default_value(f->at(0), f);
        }

    Node* ctor = nullptr;
    for (Node* c : C.ctors) if (c->at(0)->kids.size() == args.size()) { ctor = c; break; }
    if (!ctor) {
        if (!args.empty()) rt_error("no matching constructor for " + cls + " with " + std::to_string(args.size()) + " argument(s)", at);
        if (!C.base.empty()) {
            auto bi = classes.find(C.base);
            Node* bctor = nullptr;
            for (Node* c : bi->second.ctors) if (c->at(0)->kids.empty()) bctor = c;
            if (bctor) { Env e; e.self = obj; try { exec(bctor->at(2), e); } catch (ReturnSignal&) {} }
        }
        Value v; v.t = Value::OBJ; v.o = obj; return v;
    }

    Env env; env.self = obj;
    Node* params = ctor->at(0);
    for (size_t i = 0; i < args.size(); ++i)
        env.define(params->kids[i]->value, coerce(args[i], params->kids[i]->at(0)->value));

    bool base_called = false;
    for (Node* init : ctor->at(1)->kids) {
        std::vector<Value> iargs = eval_args(init->at(0), env);
        if (init->value == C.base) {
            Value tmp = construct(C.base, iargs, init);
            for (auto& kv : tmp.o->fields) obj->fields[kv.first] = kv.second;
            base_called = true;
        } else {
            auto f = obj->fields.find(init->value);
            if (f == obj->fields.end()) rt_error("no field '" + init->value + "' to initialize", init);
            if (iargs.size() != 1) rt_error("field initializer expects exactly 1 argument", init);
            assign_to(&f->second, iargs[0]);
        }
    }
    if (!base_called && !C.base.empty()) {
        Node* bctor = nullptr;
        for (Node* c : classes[C.base].ctors) if (c->at(0)->kids.empty()) bctor = c;
        if (bctor) { try { exec(bctor->at(2), env); } catch (ReturnSignal&) {} }
    }
    try { exec(ctor->at(2), env); } catch (ReturnSignal&) {}
    Value v; v.t = Value::OBJ; v.o = obj; return v;
}

static void declare(Node* n, Env& env, bool global) {
    auto put = [&](const string& name, const Value& v) {
        if (global) globals[name] = v; else env.define(name, v);
    };
    if (n->type == "Decls") { for (Node* d : n->kids) declare(d, env, global); return; }

    Node* type = n->at(0);
    if (n->type == "VarDecl") {
        if (n->at(1)) {
            Value v = eval(n->at(1), env);
            if (is_class_type(type->value)) put(n->value, deep_copy(v));
            else put(n->value, coerce(v, type->value));
        } else put(n->value, default_value(type, n));
    } else if (n->type == "ObjDecl") {
        put(n->value, construct(type->value, eval_args(n->at(1), env), n));
    } else if (n->type == "ArrayDecl") {
        Value arr; arr.t = Value::ARR; arr.a = std::make_shared<std::vector<Value>>();
        Node* init = n->at(2);
        long long sz = n->at(1)->type == "Empty" ? (long long)init->kids.size() : eval(n->at(1), env).inum();
        if (sz < 0) rt_error("negative array size", n);
        for (long long i = 0; i < sz; ++i) {
            if (init && i < (long long)init->kids.size()) arr.a->push_back(coerce(eval(init->kids[i], env), type->value));
            else arr.a->push_back(default_value(type, n));
        }
        put(n->value, arr);
    }
}

static void exec(Node* n, Env& env) {
    const string& t = n->type;
    if (t == "Block") {
        env.push();
        try { for (Node* s : n->kids) exec(s, env); }
        catch (...) { env.pop(); throw; }
        env.pop();
    }
    else if (t == "Empty" || t == "FuncProto") {}
    else if (t == "ExprStmt") eval(n->at(0), env);
    else if (t == "VarDecl" || t == "ObjDecl" || t == "ArrayDecl" || t == "Decls") declare(n, env, false);
    else if (t == "If") {
        if (eval(n->at(0), env).truthy()) exec(n->at(1), env);
        else if (n->at(2)) exec(n->at(2), env);
    }
    else if (t == "While") {
        while (eval(n->at(0), env).truthy()) {
            try { exec(n->at(1), env); }
            catch (BreakSignal&) { break; }
            catch (ContinueSignal&) { continue; }
        }
    }
    else if (t == "DoWhile") {
        do {
            try { exec(n->at(0), env); }
            catch (BreakSignal&) { break; }
            catch (ContinueSignal&) { continue; }
        } while (eval(n->at(1), env).truthy());
    }
    else if (t == "For") {
        env.push();
        try {
            exec(n->at(0), env);
            while (n->at(1)->type == "Empty" || eval(n->at(1), env).truthy()) {
                try { exec(n->at(3), env); }
                catch (BreakSignal&) { break; }
                catch (ContinueSignal&) {}
                exec(n->at(2), env);
            }
        } catch (...) { env.pop(); throw; }
        env.pop();
    }
    else if (t == "Return") { ReturnSignal r; if (n->at(0)) r.v = eval(n->at(0), env); throw r; }
    else if (t == "Break") throw BreakSignal();
    else if (t == "Continue") throw ContinueSignal();
    else if (t == "Cout") {
        for (Node* k : n->kids) {
            if (k->type == "Endl") std::cout << std::endl;
            else std::cout << show(eval(k, env));
        }
        std::cout.flush();
    }
    else if (t == "Cin") {
        for (Node* k : n->kids) {
            Value* dst = lvalue(k, env);
            if (dst->t == Value::DBL) { double d = 0; if (!(std::cin >> d)) d = 0; *dst = Value::Dbl(d); }
            else if (dst->t == Value::STR) { string s; std::cin >> s; *dst = Value::Str(s); }
            else if (dst->t == Value::CHR) { char c = 0; if (!(std::cin >> c)) c = 0; *dst = Value::Chr(c); }
            else { long long v = 0; if (!(std::cin >> v)) v = 0; assign_to(dst, Value::Int(v)); }
        }
    }
    else rt_error("cannot execute node " + t, n);
}

static void register_class(Node* c) {
    ClassInfo ci; ci.name = c->value;
    for (Node* m : c->kids) {
        if (m->type == "Base")  ci.base = m->value;
        else if (m->type == "Field") ci.fields.push_back(m);
        else if (m->type == "Method") ci.methods[m->value] = m;
        else if (m->type == "Ctor") ci.ctors.push_back(m);
    }
    classes[ci.name] = ci;
}

static void run_program_impl(Node* prog) {
    classes.clear(); functions.clear(); globals.clear(); call_depth = 0;
    Env genv;

    for (Node* d : prog->kids) if (d->type == "ClassDecl") register_class(d);
    for (Node* d : prog->kids) {
        if (d->type == "FuncDecl") functions[d->value] = d;
        else if (d->type == "MethodDef") {
            const string& cls = d->at(3)->value;
            if (!classes.count(cls)) rt_error("method definition for unknown class " + cls, d);
            classes[cls].methods[d->value] = d;
        }
    }
    for (Node* d : prog->kids)
        if (d->type == "VarDecl" || d->type == "ObjDecl" || d->type == "ArrayDecl" || d->type == "Decls")
            declare(d, genv, true);

    auto m = functions.find("main");
    if (m == functions.end()) rt_error("no main() function");
    Value r = call_user(m->second, {}, nullptr, m->second);
    (void)r;
}

static void* thread_entry(void* arg) {
    auto* data = static_cast<std::pair<Node*, std::exception_ptr>*>(arg);
    try { run_program_impl(data->first); }
    catch (...) { data->second = std::current_exception(); }
    return nullptr;
}

void run_program(Node* prog) {
    std::pair<Node*, std::exception_ptr> data{prog, nullptr};
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)1024 * 1024 * 1024);
    pthread_t th;
    if (pthread_create(&th, &attr, thread_entry, &data) != 0) { run_program_impl(prog); return; }
    pthread_join(th, nullptr);
    pthread_attr_destroy(&attr);
    if (data.second) std::rethrow_exception(data.second);
}
