#include "bb_toker.h"
#include <cctype>

namespace bb
{
    static map<string, int> alphaTokes, lowerTokes;

    static void makeKeywords()
    {
        static bool made;
        if (made) return;

        alphaTokes["Dim"] = DIM;
        alphaTokes["Goto"] = GOTO;
        alphaTokes["Gosub"] = GOSUB;
        alphaTokes["Return"] = RETURN;
        alphaTokes["Exit"] = EXIT;
        alphaTokes["If"] = IF;
        alphaTokes["Then"] = THEN;
        alphaTokes["Else"] = ELSE;
        alphaTokes["EndIf"] = ENDIF;
        alphaTokes["End If"] = ENDIF;
        alphaTokes["ElseIf"] = ELSEIF;
        alphaTokes["Else If"] = ELSEIF;
        alphaTokes["While"] = WHILE;
        alphaTokes["Wend"] = WEND;
        alphaTokes["For"] = FOR;
        alphaTokes["To"] = TO;
        alphaTokes["Step"] = STEP;
        alphaTokes["Next"] = NEXT;
        alphaTokes["Function"] = FUNCTION;
        alphaTokes["End Function"] = ENDFUNCTION;
        alphaTokes["Type"] = TYPE;
        alphaTokes["End Type"] = ENDTYPE;
        alphaTokes["Each"] = EACH;
        alphaTokes["Local"] = LOCAL;
        alphaTokes["Global"] = GLOBAL;
        alphaTokes["Field"] = FIELD;
        alphaTokes["Const"] = BBCONST;
        alphaTokes["Select"] = SELECT;
        alphaTokes["Case"] = CASE;
        alphaTokes["Default"] = DEFAULT;
        alphaTokes["End Select"] = ENDSELECT;
        alphaTokes["Repeat"] = REPEAT;
        alphaTokes["Until"] = UNTIL;
        alphaTokes["Forever"] = FOREVER;
        alphaTokes["Data"] = DATA;
        alphaTokes["Read"] = READ;
        alphaTokes["Restore"] = RESTORE;
        alphaTokes["Abs"] = ABS;
        alphaTokes["Sgn"] = SGN;
        alphaTokes["Mod"] = MOD;
        alphaTokes["Pi"] = PI;
        alphaTokes["True"] = BBTRUE;
        alphaTokes["False"] = BBFALSE;
        alphaTokes["Int"] = BBINT;
        alphaTokes["Float"] = BBFLOAT;
        alphaTokes["Str"] = BBSTR;
        alphaTokes["Include"] = INCLUDE;

        alphaTokes["New"] = BBNEW;
        alphaTokes["Delete"] = BBDELETE;
        alphaTokes["First"] = FIRST;
        alphaTokes["Last"] = LAST;
        alphaTokes["Insert"] = INSERT;
        alphaTokes["Before"] = BEFORE;
        alphaTokes["After"] = AFTER;
        alphaTokes["Null"] = BBNULL;
        alphaTokes["Object"] = OBJECT;
        alphaTokes["Handle"] = BBHANDLE;

        alphaTokes["And"] = AND;
        alphaTokes["Or"] = OR;
        alphaTokes["Xor"] = XOR;
        alphaTokes["Not"] = NOT;
        alphaTokes["Shl"] = SHL;
        alphaTokes["Shr"] = SHR;
        alphaTokes["Sar"] = SAR;

        for (map<string, int>::const_iterator it = alphaTokes.begin(); it != alphaTokes.end(); ++it)
            lowerTokes[bb_tolower(it->first)] = it->second;
        made = true;
    }

    Toker::Toker(std::istream &in) : in(in), curr_row(-1)
    {
        makeKeywords();
        nextline();
    }

    map<string, int> &Toker::getKeywords()
    {
        makeKeywords();
        return alphaTokes;
    }

    int Toker::pos()
    {
        return ((curr_row) << 16) | (tokes[curr_toke].from);
    }

    int Toker::curr()
    {
        return tokes[curr_toke].n;
    }

    string Toker::text()
    {
        int from = tokes[curr_toke].from, to = tokes[curr_toke].to;
        return line.substr(from, to - from);
    }

    int Toker::lookAhead(int n)
    {
        size_t k = curr_toke + n;
        if (k >= tokes.size()) return '\n';
        return tokes[k].n;
    }

    static inline int at(const string &s, size_t k)
    {
        return k < s.size() ? (unsigned char)s[k] : 0;
    }

    void Toker::nextline()
    {
        ++curr_row;
        curr_toke = 0;
        tokes.clear();
        if (in.eof())
        {
            line.resize(1);
            line[0] = (char)EOF;
            tokes.push_back(Toke(EOF, 0, 1));
            return;
        }

        std::getline(in, line);
        if (line.size() && line[line.size() - 1] == '\r') line.resize(line.size() - 1);
        line += '\n';

        for (size_t k = 0; k < line.size();)
        {
            int c = (unsigned char)line[k];
            int from = (int)k;
            if (c == '\n')
            {
                tokes.push_back(Toke(c, from, (int)++k));
                continue;
            }
            if (isspace(c)) { ++k; continue; }
            if (c == ';')
            {
                for (++k; at(line, k) != '\n'; ++k) {}
                continue;
            }
            if (c == '.' && isdigit(at(line, k + 1)))
            {
                for (k += 2; isdigit(at(line, k)); ++k) {}
                tokes.push_back(Toke(FLOATCONST, from, (int)k));
                continue;
            }
            if (isdigit(c))
            {
                for (++k; isdigit(at(line, k)); ++k) {}
                if (at(line, k) == '.')
                {
                    for (++k; isdigit(at(line, k)); ++k) {}
                    tokes.push_back(Toke(FLOATCONST, from, (int)k));
                    continue;
                }
                tokes.push_back(Toke(INTCONST, from, (int)k));
                continue;
            }
            if (c == '%' && (at(line, k + 1) == '0' || at(line, k + 1) == '1'))
            {
                for (k += 2; at(line, k) == '0' || at(line, k) == '1'; ++k) {}
                tokes.push_back(Toke(BINCONST, from, (int)k));
                continue;
            }
            if (c == '$' && isxdigit(at(line, k + 1)))
            {
                for (k += 2; isxdigit(at(line, k)); ++k) {}
                tokes.push_back(Toke(HEXCONST, from, (int)k));
                continue;
            }
            if (isalpha(c))
            {
                for (++k; isalnum(at(line, k)) || at(line, k) == '_'; ++k) {}

                string ident = bb_tolower(line.substr(from, k - from));

                if (at(line, k) == ' ' && isalpha(at(line, k + 1)))
                {
                    size_t t = k;
                    for (t += 2; isalnum(at(line, t)) || at(line, t) == '_'; ++t) {}
                    string s = bb_tolower(line.substr(from, t - from));
                    if (lowerTokes.find(s) != lowerTokes.end())
                    {
                        k = t;
                        ident = s;
                    }
                }

                map<string, int>::iterator it = lowerTokes.find(ident);

                if (it == lowerTokes.end())
                {
                    for (size_t n = from; n < k; ++n) line[n] = (char)tolower((unsigned char)line[n]);
                    tokes.push_back(Toke(IDENT, from, (int)k));
                    continue;
                }

                tokes.push_back(Toke(it->second, from, (int)k));
                continue;
            }
            if (c == '\"')
            {
                for (++k; at(line, k) != '\"' && at(line, k) != '\n'; ++k) {}
                if (at(line, k) == '\"') ++k;
                tokes.push_back(Toke(STRINGCONST, from, (int)k));
                continue;
            }
            int n = at(line, k + 1);
            if ((c == '<' && n == '>') || (c == '>' && n == '<'))
            {
                tokes.push_back(Toke(NE, from, (int)(k += 2)));
                continue;
            }
            if ((c == '<' && n == '=') || (c == '=' && n == '<'))
            {
                tokes.push_back(Toke(LE, from, (int)(k += 2)));
                continue;
            }
            if ((c == '>' && n == '=') || (c == '=' && n == '>'))
            {
                tokes.push_back(Toke(GE, from, (int)(k += 2)));
                continue;
            }
            tokes.push_back(Toke(c, from, (int)++k));
        }
        if (!tokes.size()) tokes.push_back(Toke('\n', 0, 1));
    }

    int Toker::next()
    {
        if ((size_t)++curr_toke == tokes.size()) nextline();
        return curr();
    }
}
