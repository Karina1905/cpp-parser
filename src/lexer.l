%option noyywrap yylineno nounput noinput

%{
#include <string>
#include <cstdlib>
#include "ast.hpp"
#include "parser.tab.hpp"

static std::string strbuf;
#define YY_USER_ACTION yylloc.first_line = yylloc.last_line = yylineno;
%}

%x STR COMMENT

DIGIT   [0-9]
ID      [A-Za-z_][A-Za-z0-9_]*

%%

"#"[^\n]*               { }
"//"[^\n]*              { }
"/*"                    { BEGIN(COMMENT); }
<COMMENT>"*/"           { BEGIN(INITIAL); }
<COMMENT>.|\n           { }

"int"       { return INT; }
"double"    { return DOUBLE; }
"float"     { return DOUBLE; }
"bool"      { return BOOL; }
"char"      { return CHAR; }
"string"    { return STRING; }
"void"      { return VOID; }
"class"     { return CLASS; }
"struct"    { return STRUCT; }
"public"    { return PUBLIC; }
"private"   { return PRIVATE; }
"protected" { return PROTECTED; }
"if"        { return IF; }
"else"      { return ELSE; }
"while"     { return WHILE; }
"do"        { return DO; }
"for"       { return FOR; }
"return"    { return RETURN; }
"break"     { return BREAK; }
"continue"  { return CONTINUE; }
"cout"      { return COUT; }
"cin"       { return CIN; }
"endl"      { return ENDL; }
"this"      { return THIS; }
"true"      { return TRUE_LIT; }
"false"     { return FALSE_LIT; }
"using"     { return USING; }
"namespace" { return NAMESPACE; }
"const"     { return CONST; }
"virtual"   { }
"override"  { }
"std"       { return STD; }

"<<"   { return SHL; }
">>"   { return SHR; }
"<="   { return LE; }
">="   { return GE; }
"=="   { return EQ; }
"!="   { return NE; }
"&&"   { return AND; }
"||"   { return OR; }
"++"   { return INC; }
"--"   { return DEC; }
"+="   { return PLUSEQ; }
"-="   { return MINUSEQ; }
"*="   { return MULEQ; }
"/="   { return DIVEQ; }
"%="   { return MODEQ; }
"->"   { return ARROW; }
"::"   { return SCOPE; }

{DIGIT}+"."{DIGIT}*([eE][-+]?{DIGIT}+)? |
{DIGIT}+[eE][-+]?{DIGIT}+     { yylval.str = strdup(yytext); return DOUBLE_LIT; }
{DIGIT}+                      { yylval.str = strdup(yytext); return INT_LIT; }

"'"\\n"'"      { yylval.str = strdup("\n"); return CHAR_LIT; }
"'"\\t"'"      { yylval.str = strdup("\t"); return CHAR_LIT; }
"'"\\\\"'"     { yylval.str = strdup("\\"); return CHAR_LIT; }
"'"\\'"'"      { yylval.str = strdup("'"); return CHAR_LIT; }
"'"[^'\\]"'"   { char b[2] = { yytext[1], 0 }; yylval.str = strdup(b); return CHAR_LIT; }

\"                { strbuf.clear(); BEGIN(STR); }
<STR>\"           { BEGIN(INITIAL); yylval.str = strdup(strbuf.c_str()); return STRING_LIT; }
<STR>\\n          { strbuf += '\n'; }
<STR>\\t          { strbuf += '\t'; }
<STR>\\\"         { strbuf += '"'; }
<STR>\\\\         { strbuf += '\\'; }
<STR>[^\\"\n]+    { strbuf += yytext; }
<STR>\n           { fprintf(stderr, "line %d: unterminated string\n", yylineno); BEGIN(INITIAL); }

{ID}            { yylval.str = strdup(yytext); return IDENT; }

[ \t\r\n]+      { }

[-+*/%<>=!;,.(){}\[\]?:&]  { return yytext[0]; }

.               { fprintf(stderr, "line %d: unexpected character '%s'\n", yylineno, yytext); }

%%
