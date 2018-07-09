
#ifndef CONDUIT_INCLUDE_REPL_H_
#define CONDUIT_INCLUDE_REPL_H_

#include "lua-wrapper.h"
#include <algorithm>

#if defined(_MSC_VER) || defined(__MINGW32__)
#include <editline/readline.h>
#include <Windows.h>
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

namespace conduit {
namespace lua {
    enum class ReplExit {
        NONE,
        ABORT,
        CONTINUE,
        EXIT,
    };
    struct LuaGlobal {
        static inline lua_State *&lua()
        {
            static lua_State *l = nullptr;
            return l;
        }
    };

    inline char **completions(const char *text, int start, int)
    {
        lua_State *global_L = LuaGlobal::lua();
        static std::locale locale;
        std::vector<std::string> keys{};
        auto s = std::string(text);

        {
            int last_punct = 0;
            for (unsigned i = 0; i < s.size(); ++i) {
                if (::ispunct(s[i]) && (s[i] != '_')) {
                    last_punct = i;
                }
            }
            s = s.substr(last_punct);
        }

        if (global_L == nullptr || s.size() == 0)
            goto out;

        {
            lua_getglobal(global_L, "_G");
            lua_pushnil(global_L);
            std::vector<std::tuple<int, std::string>> matches;
            while (lua_next(global_L, -2)) {
                std::string key = conduit::pop_arg(global_L, -2, (std::string *)nullptr);
                if (key.find(s) != std::string::npos) {
                    matches.push_back(std::make_tuple(static_cast<int>(key.find(s)), key));
                }
                lua_pop(global_L, 1);
            }
            lua_pop(global_L, 1);

            // Grab previous command lines
            if (start == 0) {
                #if defined(_MSC_VER) || defined(__MINGW32__)
                const int h_l = history_length();
                #else
                const int h_l = history_length;
                #endif
                for (int i = 0; i < h_l; ++i) {
                    #if defined(_MSC_VER) || defined(__MINGW32__)
                    if (history_get(i)->line == nullptr)
                        continue;
                    #else
                    if (history_get(i) == nullptr || history_get(i)->line == nullptr)
                        continue;
                    #endif
                    std::string h = history_get(i)->line;
                    if (h.size() == 0)
                        continue;
                    if (h.find(s) != std::string::npos) {
                        matches.push_back(std::make_tuple(static_cast<int>(h.find(s)), h));
                    }
                }
            }

            std::sort(matches.begin(), matches.end(), [](std::tuple<int, std::string> l, std::tuple<int, std::string> r) {
                return std::get<0>(l) < std::get<0>(r);
            });
            std::for_each(matches.begin(), matches.end(), [&](std::tuple<int, std::string> p) {
                keys.push_back(std::get<1>(p));
            });
        }

    out:
        #if (!defined(_MSC_VER)) && (!defined(__MINGW32__))
        if (keys.size() == 0)
            return nullptr;
        #endif
        auto ret = (char **)malloc(sizeof(char *) * (keys.size() + 1));
        for (unsigned i = 0; i < keys.size(); ++i) {
            ret[i] = (char *)malloc(keys[i].size() + 1);
            memcpy(ret[i], keys[i].c_str(), keys[i].size());
            ret[i][keys[i].size()] = '\0';
        }
        ret[keys.size()] = nullptr;
        return ret;
    }

    inline ReplExit start_lua_repl()
    {
        ReplExit exit_reason = ReplExit::NONE;
        lua_State *L = LuaGlobal::lua();
        while (true) {
            char *s = readline("--> ");
            if (s == nullptr)
                break;
            if (strcmp(s, "cont") == 0) {
                exit_reason = ReplExit::CONTINUE;
                free(s);
                break;
            }
            if (strcmp(s, "quit") == 0 || strcmp(s, "exit") == 0) {
                exit_reason = ReplExit::EXIT;
                free(s);
                break;
            }
            if (strcmp(s, "abort") == 0) {
                exit_reason = ReplExit::ABORT;
                free(s);
                break;
            }

            int error = luaL_dostring(L, (std::string("return ") + s).c_str());
            if (error) {
                lua_pop(L, -1);
                error = luaL_dostring(L, s);
            }
            if (error) {
                std::cerr << lua_tostring(L, -1) << std::endl;
                lua_pop(L, 1);
            }
            add_history(s);
            free(s);
            // No error
            if (lua_gettop(L)) {
                conduit::stack_dump(L);
                lua_pop(L, lua_gettop(L));
            }
        }
        return exit_reason;
    }
}
}

#endif
