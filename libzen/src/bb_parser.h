/*
** bb_parser.h — The parser builds an abstract syntax tree from tokens.
** (port of Blitz3D compiler/parser.h)
*/
#ifndef BB_PARSER_H
#define BB_PARSER_H

#include "bb_toker.h"
#include "bb_nodes.h"

namespace bb
{
    class Parser
    {
    public:
        Parser(Toker &t);

        ProgNode *parse(const string &main);

    private:
        string incfile;
        set<string> included;
        Toker *toker, *main_toker;
        map<string, DimNode *> arrayDecls;

        DeclSeqNode *consts;
        DeclSeqNode *structs;
        DeclSeqNode *funcs;
        DeclSeqNode *datas;

        StmtSeqNode *parseStmtSeq(int scope);
        void parseStmtSeq(StmtSeqNode *stmts, int scope);

        void ex(const string &s);
        void exp(const string &s);

        string parseIdent();
        void parseChar(int c);
        string parseTypeTag();

        VarNode *parseVar();
        VarNode *parseVar(const string &ident, const string &tag);
        IfNode *parseIf();

        DeclNode *parseVarDecl(int kind, bool constant);
        DimNode *parseArrayDecl();
        DeclNode *parseFuncDecl();
        DeclNode *parseStructDecl();

        ExprSeqNode *parseExprSeq();

        ExprNode *parseExpr(bool opt);
        ExprNode *parseExpr1(bool opt); /* And, Or, Xor */
        ExprNode *parseExpr2(bool opt); /* <,=,>,<=,<>,>= */
        ExprNode *parseExpr3(bool opt); /* +,- */
        ExprNode *parseExpr4(bool opt); /* Shl,Shr,Sar */
        ExprNode *parseExpr5(bool opt); /* *,/,Mod */
        ExprNode *parseExpr6(bool opt); /* ^ */
        ExprNode *parseUniExpr(bool opt); /* +,-,Not,~ */
        ExprNode *parsePrimary(bool opt);
    };
}

#endif
