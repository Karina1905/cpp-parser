
%code requires {
    #include "ast.hpp"
}

%{
#include <cstdio>
#include <cstring>
#include <string>
#include "ast.hpp"

extern int yylex();
extern int yylineno;
extern char* yytext;
void yyerror(const char* s);

Node* ast_root = nullptr;

static Node* attach_type(Node* type, Node* decls) {
    for (Node* d : decls->kids) {
        Node* t = new Node(*type);
        d->kids.insert(d->kids.begin(), t);
    }
    if (decls->kids.size() == 1) return decls->kids[0];
    return decls;
}

static Node* fields_from(Node* d) {
    Node* fs = new Node("Fields");
    std::vector<Node*> items = (d->type == "Decls") ? d->kids : std::vector<Node*>{d};
    for (Node* v : items) {
        Node* f = new Node("Field", v->value, v->line);
        f->add(v->at(0));
        if (v->type == "ArrayDecl") { Node* dim = new Node("Dim"); dim->add(v->at(1)); f->add(dim); }
        else if (v->at(1)) f->add(v->at(1));
        fs->add(f);
    }
    return fs;
}
%}

%define parse.error verbose
%locations

%union {
    char* str;
    Node* node;
}

%token INT DOUBLE BOOL CHAR STRING VOID CLASS STRUCT PUBLIC PRIVATE PROTECTED
%token IF ELSE WHILE DO FOR RETURN BREAK CONTINUE COUT CIN ENDL THIS
%token TRUE_LIT FALSE_LIT USING NAMESPACE CONST STD
%token SHL SHR LE GE EQ NE AND OR INC DEC
%token PLUSEQ MINUSEQ MULEQ DIVEQ MODEQ ARROW SCOPE
%token <str> IDENT INT_LIT DOUBLE_LIT STRING_LIT CHAR_LIT

%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%right '=' PLUSEQ MINUSEQ MULEQ DIVEQ MODEQ
%right '?' ':'
%left OR
%left AND
%left EQ NE
%left '<' '>' LE GE
%left '+' '-'
%left '*' '/' '%'
%right '!' UMINUS CAST PREFIX
%left INC DEC '.' '[' ARROW

%type <node> program decl_list decl type cast_type func_decl method_def params param_list param
%type <node> class_decl class_kw base_opt members member init_list_opt init_items init_item
%type <node> block stmts stmt var_decl_stmt declarator_list declarator
%type <node> if_stmt while_stmt do_stmt for_stmt for_init for_cond for_step
%type <node> cout_stmt shl_items out_item cin_stmt shr_items
%type <node> expr args arg_list

%start program

%%

program
    : decl_list                          { ast_root = $1; }
    ;

decl_list
    :                     { $$ = mk("Program"); }
    | decl_list decl                     { $$ = list_append($1, $2); }
    ;

decl
    : func_decl                          { $$ = $1; }
    | method_def                         { $$ = $1; }
    | class_decl                         { $$ = $1; }
    | var_decl_stmt                      { $$ = $1; }
    | type IDENT '(' params ')' ';'      { $$ = mk("FuncProto", $2); $$->add($1)->add($4); free($2); }
    | USING NAMESPACE IDENT ';'          { $$ = nullptr; free($3); }
    | USING NAMESPACE STD ';'            { $$ = nullptr; }
    ;

type
    : INT                                { $$ = mk("Type", "int"); }
    | DOUBLE                             { $$ = mk("Type", "double"); }
    | BOOL                               { $$ = mk("Type", "bool"); }
    | CHAR                               { $$ = mk("Type", "char"); }
    | STRING                             { $$ = mk("Type", "string"); }
    | VOID                               { $$ = mk("Type", "void"); }
    | IDENT                              { $$ = mk("Type", $1); free($1); }
    | CONST type                         { $$ = $2; }
    | STD SCOPE type                     { $$ = $3; }
    ;

cast_type
    : INT                                { $$ = mk("Type", "int"); }
    | DOUBLE                             { $$ = mk("Type", "double"); }
    | CHAR                               { $$ = mk("Type", "char"); }
    | BOOL                               { $$ = mk("Type", "bool"); }
    ;

func_decl
    : type IDENT '(' params ')' block
        { $$ = mk("FuncDecl", $2); $$->line = @2.first_line; $$->add($1)->add($4)->add($6); free($2); }
    ;

method_def
    : type IDENT SCOPE IDENT '(' params ')' block
        { $$ = mk("MethodDef", $4); $$->add($1)->add($6)->add($8)->add(mk("ClassRef", $2));
          free($2); free($4); }
    ;

params
    :                     { $$ = mk("Params"); }
    | VOID                               { $$ = mk("Params"); }
    | param_list                         { $$ = $1; }
    ;

param_list
    : param                              { $$ = mk1("Params", $1); }
    | param_list ',' param               { $$ = list_append($1, $3); }
    ;

param
    : type IDENT                         { $$ = mk1("Param", $1); $$->value = $2; free($2); }
    | type '&' IDENT                     { $$ = mk1("Param", $1); $$->value = $3; free($3); }
    | type IDENT '[' ']'                 { $1->value += "[]"; $$ = mk1("Param", $1); $$->value = $2; free($2); }
    ;

class_decl
    : class_kw IDENT base_opt '{' members '}' ';'
        { $$ = mk("ClassDecl", $2); $$->add($3);
          for (Node* m : $5->kids) {
              if (m->type == "Fields") { for (Node* f : m->kids) $$->add(f); delete m; }
              else $$->add(m);
          }
          delete $5; free($2); }
    ;

class_kw
    : CLASS                              { $$ = nullptr; }
    | STRUCT                             { $$ = nullptr; }
    ;

base_opt
    :                     { $$ = nullptr; }
    | ':' PUBLIC IDENT                   { $$ = mk("Base", $3); free($3); }
    | ':' PRIVATE IDENT                  { $$ = mk("Base", $3); free($3); }
    | ':' PROTECTED IDENT                { $$ = mk("Base", $3); free($3); }
    | ':' IDENT                          { $$ = mk("Base", $2); free($2); }
    ;

members
    :                     { $$ = mk("Members"); }
    | members member                     { $$ = list_append($1, $2); }
    ;

member
    : PUBLIC ':'                         { $$ = nullptr; }
    | PRIVATE ':'                        { $$ = nullptr; }
    | PROTECTED ':'                      { $$ = nullptr; }
    | type declarator_list ';'           { $$ = fields_from(attach_type($1, $2)); delete $1; }
    | func_decl                          { $$ = $1; $$->type = "Method"; }
    | type IDENT '(' params ')' ';'      { $$ = mk("MethodProto", $2); $$->add($1)->add($4); free($2); }
    | IDENT '(' params ')' init_list_opt block
        { $$ = mk("Ctor", $1); $$->add($3)->add($5)->add($6); free($1); }
    ;

init_list_opt
    :                     { $$ = mk("InitList"); }
    | ':' init_items                     { $$ = $2; }
    ;

init_items
    : init_item                          { $$ = mk1("InitList", $1); }
    | init_items ',' init_item           { $$ = list_append($1, $3); }
    ;

init_item
    : IDENT '(' args ')'                 { $$ = mk1("Init", $3); $$->value = $1; free($1); }
    ;

block
    : '{' stmts '}'                      { $$ = $2; }
    ;

stmts
    :                     { $$ = mk("Block"); }
    | stmts stmt                         { $$ = list_append($1, $2); }
    ;

stmt
    : var_decl_stmt                      { $$ = $1; }
    | type IDENT '(' ')' ';'             { $$ = mk("FuncProto", $2); $$->add($1)->add(mk("Params")); free($2); }
    | expr ';'                           { $$ = mk1("ExprStmt", $1); }
    | ';'                                { $$ = mk("Empty"); }
    | block                              { $$ = $1; }
    | if_stmt                            { $$ = $1; }
    | while_stmt                         { $$ = $1; }
    | do_stmt                            { $$ = $1; }
    | for_stmt                           { $$ = $1; }
    | RETURN ';'                         { $$ = mk("Return"); }
    | RETURN expr ';'                    { $$ = mk1("Return", $2); }
    | BREAK ';'                          { $$ = mk("Break"); }
    | CONTINUE ';'                       { $$ = mk("Continue"); }
    | cout_stmt                          { $$ = $1; }
    | cin_stmt                           { $$ = $1; }
    ;

var_decl_stmt
    : type declarator_list ';'           { $$ = attach_type($1, $2); delete $1; }
    ;

declarator_list
    : declarator                         { $$ = mk1("Decls", $1); }
    | declarator_list ',' declarator     { $$ = list_append($1, $3); }
    ;

declarator
    : IDENT                                       { $$ = mk("VarDecl", $1); free($1); }
    | IDENT '=' expr                              { $$ = mk1("VarDecl", $3); $$->value = $1; free($1); }
    | IDENT '(' arg_list ')'                      { $$ = mk1("ObjDecl", $3); $$->value = $1; free($1); }
    | IDENT '[' expr ']'                          { $$ = mk1("ArrayDecl", $3); $$->value = $1; free($1); }
    | IDENT '[' expr ']' '=' '{' args '}'         { $$ = mk2("ArrayDecl", $1, $3, $7); free($1); }
    | IDENT '[' ']' '=' '{' args '}'              { $$ = mk2("ArrayDecl", $1, mk("Empty"), $6); free($1); }
    ;

if_stmt
    : IF '(' expr ')' stmt %prec LOWER_THAN_ELSE  { $$ = mk2("If", "", $3, $5); }
    | IF '(' expr ')' stmt ELSE stmt              { $$ = mk2("If", "", $3, $5); $$->add($7); }
    ;

while_stmt
    : WHILE '(' expr ')' stmt            { $$ = mk2("While", "", $3, $5); }
    ;

do_stmt
    : DO stmt WHILE '(' expr ')' ';'     { $$ = mk2("DoWhile", "", $2, $5); }
    ;

for_stmt
    : FOR '(' for_init for_cond ';' for_step ')' stmt
        { $$ = mk("For"); $$->add($3)->add($4)->add($6)->add($8); }
    ;

for_init
    : var_decl_stmt                      { $$ = $1; }
    | expr ';'                           { $$ = mk1("ExprStmt", $1); }
    | ';'                                { $$ = mk("Empty"); }
    ;

for_cond
    :                     { $$ = mk("Empty"); }
    | expr                               { $$ = $1; }
    ;

for_step
    :                     { $$ = mk("Empty"); }
    | expr                               { $$ = mk1("ExprStmt", $1); }
    ;

cout_stmt
    : COUT shl_items ';'                 { $$ = $2; }
    | STD SCOPE COUT shl_items ';'       { $$ = $4; }
    ;

shl_items
    : SHL out_item                       { $$ = mk1("Cout", $2); }
    | shl_items SHL out_item             { $$ = list_append($1, $3); }
    ;

out_item
    : expr                               { $$ = $1; }
    | ENDL                               { $$ = mk("Endl"); }
    | STD SCOPE ENDL                     { $$ = mk("Endl"); }
    ;

cin_stmt
    : CIN shr_items ';'                  { $$ = $2; }
    | STD SCOPE CIN shr_items ';'        { $$ = $4; }
    ;

shr_items
    : SHR expr                           { $$ = mk1("Cin", $2); }
    | shr_items SHR expr                 { $$ = list_append($1, $3); }
    ;

expr
    : expr '=' expr                      { $$ = mk2("Assign", "=",  $1, $3); }
    | expr PLUSEQ expr                   { $$ = mk2("Assign", "+=", $1, $3); }
    | expr MINUSEQ expr                  { $$ = mk2("Assign", "-=", $1, $3); }
    | expr MULEQ expr                    { $$ = mk2("Assign", "*=", $1, $3); }
    | expr DIVEQ expr                    { $$ = mk2("Assign", "/=", $1, $3); }
    | expr MODEQ expr                    { $$ = mk2("Assign", "%=", $1, $3); }
    | expr '?' expr ':' expr             { $$ = mk2("Ternary", "", $1, $3); $$->add($5); }
    | expr OR expr                       { $$ = mk2("BinOp", "||", $1, $3); }
    | expr AND expr                      { $$ = mk2("BinOp", "&&", $1, $3); }
    | expr EQ expr                       { $$ = mk2("BinOp", "==", $1, $3); }
    | expr NE expr                       { $$ = mk2("BinOp", "!=", $1, $3); }
    | expr '<' expr                      { $$ = mk2("BinOp", "<",  $1, $3); }
    | expr '>' expr                      { $$ = mk2("BinOp", ">",  $1, $3); }
    | expr LE expr                       { $$ = mk2("BinOp", "<=", $1, $3); }
    | expr GE expr                       { $$ = mk2("BinOp", ">=", $1, $3); }
    | expr '+' expr                      { $$ = mk2("BinOp", "+",  $1, $3); }
    | expr '-' expr                      { $$ = mk2("BinOp", "-",  $1, $3); }
    | expr '*' expr                      { $$ = mk2("BinOp", "*",  $1, $3); }
    | expr '/' expr                      { $$ = mk2("BinOp", "/",  $1, $3); }
    | expr '%' expr                      { $$ = mk2("BinOp", "%",  $1, $3); }
    | '-' expr %prec UMINUS              { $$ = mk1("UnOp", $2); $$->value = "-"; }
    | '!' expr                           { $$ = mk1("UnOp", $2); $$->value = "!"; }
    | '(' cast_type ')' expr %prec CAST  { $$ = mk2("Cast", $2->value, $4, nullptr); delete $2; }
    | INC expr %prec PREFIX              { $$ = mk1("IncDec", $2); $$->value = "++pre"; }
    | DEC expr %prec PREFIX              { $$ = mk1("IncDec", $2); $$->value = "--pre"; }
    | expr INC                           { $$ = mk1("IncDec", $1); $$->value = "post++"; }
    | expr DEC                           { $$ = mk1("IncDec", $1); $$->value = "post--"; }
    | expr '[' expr ']'                  { $$ = mk2("Index", "", $1, $3); }
    | expr '.' IDENT                     { $$ = mk1("Member", $1); $$->value = $3; free($3); }
    | expr ARROW IDENT                   { $$ = mk1("Member", $1); $$->value = $3; free($3); }
    | expr '.' IDENT '(' args ')'        { $$ = mk2("MethodCall", $3, $1, $5); free($3); }
    | expr ARROW IDENT '(' args ')'      { $$ = mk2("MethodCall", $3, $1, $5); free($3); }
    | IDENT '(' args ')'                 { $$ = mk1("Call", $3); $$->value = $1; $$->line = @1.first_line; free($1); }
    | IDENT                              { $$ = mk("Ident", $1); $$->line = @1.first_line; free($1); }
    | THIS                               { $$ = mk("This"); }
    | INT_LIT                            { $$ = mk("IntLit", $1); free($1); }
    | DOUBLE_LIT                         { $$ = mk("DoubleLit", $1); free($1); }
    | STRING_LIT                         { $$ = mk("StringLit", $1); free($1); }
    | CHAR_LIT                           { $$ = mk("CharLit", $1); free($1); }
    | TRUE_LIT                           { $$ = mk("BoolLit", "true"); }
    | FALSE_LIT                          { $$ = mk("BoolLit", "false"); }
    | '(' expr ')'                       { $$ = $2; }
    ;

args
    :                     { $$ = mk("Args"); }
    | arg_list                           { $$ = $1; }
    ;

arg_list
    : expr                               { $$ = mk1("Args", $1); }
    | arg_list ',' expr                  { $$ = list_append($1, $3); }
    ;

%%

Node* mk(const std::string& t, const std::string& v) { return new Node(t, v, yylineno); }
Node* mk1(const std::string& t, Node* a) { Node* n = mk(t); n->add(a); return n; }
Node* mk2(const std::string& t, const std::string& v, Node* a, Node* b) {
    Node* n = mk(t, v); n->add(a); n->add(b); return n;
}
Node* list_append(Node* list, Node* item) { if (item) list->kids.push_back(item); return list; }

void yyerror(const char* s) {
    fprintf(stderr, "Syntax error at line %d near '%s': %s\n", yylineno, yytext, s);
}
