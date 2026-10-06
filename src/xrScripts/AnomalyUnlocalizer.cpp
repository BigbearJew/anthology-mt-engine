#include "stdafx.h"
#include "AnomalyUnlocalizer.h"
#include <string_view>

namespace
{
xr_map<xr_string, xr_set<xr_string>> declarations;
bool loaded = false;

xr_string Lower(LPCSTR text)
{
    xr_string result(text ? text : "");
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}

void Load()
{
    loaded = true;
    auto files = FS.file_list_open("$game_config$", "unlocalizers\\", FS_RootOnly | FS_ListFiles);
    if (!files) return;
    for (LPCSTR name : *files)
    {
        const xr_string lower = Lower(name);
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != ".ltx") continue;
        string_path path;
        FS.update_path(path, "$game_config$", (xr_string("unlocalizers\\") + name).c_str());
        CInifile ini(path);
        for (const auto section : ini.sections())
        {
            auto& names = declarations[Lower(section->Name.c_str())];
            for (const auto& item : section->Data)
                names.insert(item.first.c_str());
        }
    }
    FS.file_list_close(files);
    Msg("* Anomaly unlocalizers: %u script namespaces", u32(declarations.size()));
}

bool NameStart(char c)
{
    return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool NameChar(char c) { return NameStart(c) || (c >= '0' && c <= '9'); }
bool Space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

// Track quoted strings and arbitrary Lua long brackets across lines. Declarations
// inside comments/strings must not be changed, even if they start in column zero.
struct Lexer
{
    int longEquals = -1;
    char quote = 0;
    bool Normal() const { return longEquals < 0 && quote == 0; }
    static size_t Bracket(std::string_view line, size_t at, char bracket, int& equals)
    {
        if (at >= line.size() || line[at] != bracket) return 0;
        size_t end = at + 1;
        while (end < line.size() && line[end] == '=') ++end;
        if (end >= line.size() || line[end] != bracket) return 0;
        equals = int(end - at - 1);
        return end - at + 1;
    }
    void Scan(std::string_view line)
    {
        for (size_t i = 0; i < line.size();)
        {
            int equals = 0;
            if (longEquals >= 0)
            {
                const size_t n = Bracket(line, i, ']', equals);
                if (n && equals == longEquals) { longEquals = -1; i += n; }
                else ++i;
            }
            else if (quote)
            {
                if (line[i] == '\\') i += 2;
                else { if (line[i] == quote) quote = 0; ++i; }
            }
            else if (line[i] == '-' && i + 1 < line.size() && line[i + 1] == '-')
            {
                const size_t n = Bracket(line, i + 2, '[', equals);
                if (!n) break;
                longEquals = equals;
                i += n + 2;
            }
            else if (const size_t n = Bracket(line, i, '[', equals))
            {
                longEquals = equals;
                i += n;
            }
            else
            {
                if (line[i] == '\'' || line[i] == '"') quote = line[i];
                ++i;
            }
        }
    }
};

// Match Anomaly's column-zero declarations. A mixed local a,b declaration is
// exposed as a unit, preserving RHS evaluation and closures just as in Anomaly.
bool Rewrite(std::string_view line, const xr_set<xr_string>& names, xr_string& output)
{
    if (line.size() <= 5 || line.substr(0, 5) != "local" || !Space(line[5])) return false;
    size_t pos = 5;
    auto whitespace = [&]() { while (pos < line.size() && Space(line[pos])) ++pos; };
    auto identifier = [&]() {
        const size_t start = pos;
        if (pos < line.size() && NameStart(line[pos]))
        {
            ++pos;
            while (pos < line.size() && NameChar(line[pos])) ++pos;
        }
        return xr_string(line.data() + start, pos - start);
    };
    whitespace();
    const size_t declaration = pos;
    xr_string name = identifier();
    if (name == "function")
    {
        whitespace();
        name = identifier();
        whitespace();
        if (pos == line.size() || line[pos] != '(' || !names.count(name)) return false;
        output.append(line.data() + declaration, line.size() - declaration);
        return true;
    }
    bool match = false;
    for (;;)
    {
        if (name.empty()) return false;
        match = match || names.count(name) != 0;
        whitespace();
        if (pos == line.size() || line[pos] != ',') break;
        ++pos;
        whitespace();
        name = identifier();
    }
    if (!match) return false;
    if (pos < line.size() && line[pos] == '=')
        output.append(line.data() + declaration, line.size() - declaration);
    else if (pos == line.size() || line[pos] == ';' || line.substr(pos, 2) == "--")
    {
        output.append(line.data() + declaration, pos - declaration);
        output += "= nil ";
        output.append(line.data() + pos, line.size() - pos);
    }
    else return false;
    return true;
}
}

void AnomalyUnlocalizer::Reset()
{
    declarations.clear();
    loaded = false;
}

bool AnomalyUnlocalizer::Apply(LPCSTR nameSpace, LPCSTR source, size_t size, xr_string& output)
{
    output.clear();
    if (!nameSpace || !source || !size || source[0] == '\x1b') return false;
    if (!loaded) Load();
    const auto found = declarations.find(Lower(nameSpace));
    if (found == declarations.end()) return false;
    output.reserve(size);
    Lexer lexer;
    bool changed = false;
    for (size_t start = 0; start < size;)
    {
        size_t end = start;
        while (end < size && source[end] != '\n') ++end;
        const std::string_view line(source + start, end - start);
        if (lexer.Normal() && Rewrite(line, found->second, output)) changed = true;
        else output.append(line.data(), line.size());
        lexer.Scan(line);
        if (end < size) output += '\n';
        start = end + 1;
    }
    return changed;
}
