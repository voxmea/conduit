
// Declares clang::SyntaxOnlyAction.
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
// Declares llvm::cl::extrahelp.
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_os_ostream.h>

#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <tuple>
#include <numeric>
#ifdef _MSC_VER
#include <filesystem>
#elif __APPLE__
#include <libgen.h>
#else
#include <experimental/filesystem>
#endif
#include <fstream>

using namespace clang::tooling;
using namespace clang::ast_matchers;
using namespace clang;
using namespace llvm;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory AnalyzeToolCategory("Conduit Analysis");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp
    MoreHelp("\nThis tool parses conduit code, creating a call chain through channels.");

static cl::opt<bool> colorize("color", cl::cat(AnalyzeToolCategory),
                              cl::desc("colorize the output"), cl::init(true));
static cl::opt<bool> state_check("state_check", cl::cat(AnalyzeToolCategory),
                                 cl::desc("track memvar refs"), cl::init(true));
static cl::opt<std::string> output_filename("o", cl::cat(AnalyzeToolCategory),
                                            cl::init("conduit.dot"), cl::desc("output filename"));
static cl::opt<int> seed("seed", cl::cat(AnalyzeToolCategory), cl::init(-1),
                         cl::desc("random color seed"));

// this join also supports very basic line wrapping. Although ugly to do it
// here, it's easier because the input is already tokenized.
template <typename T>
std::string join(const T &iterable, std::string delim, int wrap = std::numeric_limits<int>::max())
{
    int wrap_offset = 1;
    std::ostringstream stream;
    bool first = true;
    for (auto &i : iterable) {
        if (!first) stream << delim << " ";

        if (stream.str().size() > (wrap * wrap_offset)) {
            ++wrap_offset;
            stream << "\n";
        }

        stream << i;
        first = false;
    }
    return stream.str();
};

// maps ProperName (hook dests) to hook variable state.
struct ProperName
{
    std::string name;
    std::string unit;
    std::set<std::string> context;
    ProperName() {}
    ProperName(std::string n_) : name(std::move(n_)) {}
    ProperName(std::string n_, std::string u_) : name(std::move(n_)), unit(std::move(u_)) {}
    friend bool operator<(const ProperName &l, const ProperName &r)
    {
        return std::tie(l.name, l.unit) < std::tie(r.name, r.unit);
    }
    friend std::ostream &operator<<(std::ostream &stream, const ProperName &pn)
    {
        stream << pn.name;
        if (pn.context.size()) stream << "\n(" << join(pn.context, ",", 40) << ")";
        return stream;
    }
};

std::map<ProperName, std::set<std::string>> hook_vars;

struct Channel
{
    struct Ref
    {
        ProperName name;
        std::string timing;
        friend bool operator<(const Ref &l, const Ref &r)
        {
            return std::tie(l.name, l.timing) < std::tie(r.name, r.timing);
        }
    };
    std::set<Ref> refs;
    std::set<ProperName> dests;
};
using ChannelMap = std::map<std::string, Channel>;
using ChannelNodeMap = std::map<void *, std::string>;
ChannelMap channels;

template <typename T> std::string to_string(T *t, const clang::ASTContext *context)
{
    auto &sm = context->getSourceManager();
    auto &lo = context->getLangOpts();
    auto range = CharSourceRange::getTokenRange(t->getSourceRange());
    auto loc_begin = sm.getDecomposedLoc(sm.getSpellingLoc(range.getBegin()));
    auto loc_end = sm.getDecomposedLoc(sm.getSpellingLoc(range.getEnd()));
    // std::cout << loc_begin.second << ":" << loc_end.second << std::endl;
    return std::string(sm.getCharacterData(sm.getSpellingLoc(range.getBegin())),
                       loc_end.second - loc_begin.second);
}

std::vector<std::string> split(const std::string &s, const std::string &delim = " \n\t\r")
{
    std::vector<std::string> ret;
    for (auto begin = s.begin(); begin != s.end();) {
        auto pos = std::find_first_of(begin, s.end(), delim.begin(), delim.end());
        if (begin != pos) ret.emplace_back(begin, pos);
        if (pos == s.end()) break;
        begin = std::next(pos);
    }
    return ret;
}

inline bool ends_with(std::string s, std::string p)
{
    auto i = s.rfind(p);
    return i != std::string::npos && ((i + p.size()) == s.size());
};

ProperName find_proper_name(const FunctionDecl *decl, const clang::ASTContext *context)
{
    if (!decl->isThisDeclarationADefinition()) decl = decl->getDefinition();
    auto text = to_string(decl->getBody(), context);
    auto parts = split(text);
    std::ostringstream stream;
    if ((parts.size() >= 2) && (parts[1] == "/**csa:")) {
        auto begin = text.find("/**csa:") + 7;
        auto end = text.find("*/");
        auto parts = split(text.substr(begin, end - begin), " ");
        std::for_each(parts.begin(), parts.end(), [&stream](auto &part) { stream << part << ' '; });
    } else {
        stream << decl->getNameInfo().getName().getAsString();
    }
    auto &sm = context->getSourceManager();
    auto loc = context->getFullLoc(sm.getSpellingLoc(decl->getLocStart()));
    if (!loc.isValid()) {
        std::cout << "ERROR: found FunctionDecl but invalid location\n";
        return {};
    }
#ifdef _MSC_VER
    std::tr2::sys::path path_ = sm.getFilename(loc).str();
    std::string path = path_.filename().string();
#else
    auto path_ = sm.getFilename(loc).str();
    char *buf = new char[path_.size() + 1];
    memcpy(buf, path_.c_str(), path_.size());
    buf[path_.size()] = '\0';
    std::string path = ::basename(buf);
    delete[] buf;
#endif
    stream << ":" << path << "!" << loc.getSpellingLineNumber();
    return {stream.str(), path};
}

template <typename T> const FunctionDecl *find_func(const T *t, ASTContext *context)
{
    const FunctionDecl *ret = nullptr;
    auto parents = context->getParents(*t);
    for (auto &p : parents) {
        auto f = p.template get<clang::FunctionDecl>();
        if (f) return f;
        if (auto stmt = p.template get<clang::Stmt>()) {
            ret = find_func(stmt, context);
        } else if (auto decl = p.template get<clang::Decl>()) {
            ret = find_func(decl, context);
        }
        if (ret != nullptr) {
            break;
        }
    }
    return ret;
}

const clang::StringLiteral *find_string_literal(const Stmt *s)
{
    if (auto lit = llvm::dyn_cast<clang::StringLiteral>(s)) return lit;
    for (auto i = s->child_begin(); i != s->child_end(); ++i) {
        if (auto lit = find_string_literal(*i)) return lit;
    }
    return nullptr;
}

const FunctionDecl *find_hook_target(const MatchFinder::MatchResult &result)
{
    const FunctionDecl *dest_func = nullptr;
    if (auto lambda = result.Nodes.getNodeAs<clang::LambdaExpr>("lambda")) {
        dest_func = lambda->getCallOperator()->getCanonicalDecl()->getAsFunction();
    } else if (auto method = result.Nodes.getNodeAs<clang::CXXMethodDecl>("method")) {
        dest_func = method->getCanonicalDecl()->getAsFunction();
    } else if (auto func = result.Nodes.getNodeAs<clang::DeclRefExpr>("func")) {
        dest_func = func->getFoundDecl()->getAsFunction();
    } else if (auto bound_method = result.Nodes.getNodeAs<clang::DeclRefExpr>("bound_method")) {
        dest_func = bound_method->getFoundDecl()->getAsFunction();
    }
    return dest_func;
}

const CXXRecordDecl *get_method_context(const CXXMethodDecl *decl)
{
    auto parent = decl->getParent();
    std::string decl_context = parent->getQualifiedNameAsString();
    if (parent->isLambda()) {
        if (parent->getParentFunctionOrMethod()) {
            parent
                = llvm::dyn_cast<CXXRecordDecl>(parent->getParentFunctionOrMethod()->getParent());
        } else {
            parent = llvm::dyn_cast<CXXRecordDecl>(parent->getParent());
        }
    }
    if (parent == nullptr) {
        if (decl->getParent()->isLambda()) {
            // lambda outside of class context
            return decl->getParent();
        }
        std::cout << "parent is nullptr?!?\n";
        decl->dumpColor();
        ::exit(0);
    }
    return parent;
}

class AnalyzeFrontendConsumer : public ASTConsumer
{
    ChannelNodeMap channel_producer_map;
    std::map<const CXXMethodDecl *, ProperName> channel_consumer_map;

    struct ChannelDecl : MatchFinder::MatchCallback
    {
        ChannelMap &channels;
        ChannelNodeMap &channel_producer_map;
        ChannelDecl(ChannelMap &c_, ChannelNodeMap &cp_) : channels(c_), channel_producer_map(cp_)
        {
        }

        void run(const MatchFinder::MatchResult &result) override
        {
            std::string name;
            auto fd = result.Nodes.getNodeAs<clang::ValueDecl>("ci");
            if (fd == nullptr) {
                std::cerr << "ERROR: fd nullptr\n";
                ::exit(0);
            }
            name = fd->getNameAsString();
            auto key = (void *)fd;
            auto lit = result.Nodes.getNodeAs<clang::StringLiteral>("lit");
            if (lit == nullptr) {
                std::cerr << "ERROR: lit nullptr\n";
                fd->dumpColor();
                ::exit(0);
            }
            channel_producer_map[key] = lit->getString().str();
            std::cout << "decl matched: " << name << " : " << lit->getString().str() << " (" << key
                      << ")" << std::endl;
        }
    };
    ChannelDecl decl{channels, channel_producer_map};

    struct ChannelAssignment : MatchFinder::MatchCallback
    {
        ChannelMap &channels;
        ChannelNodeMap &channel_producer_map;
        ChannelAssignment(ChannelMap &c_, ChannelNodeMap &cp_)
            : channels(c_), channel_producer_map(cp_)
        {
        }

        void run(const MatchFinder::MatchResult &result) override
        {
            auto lvalue = result.Nodes.getNodeAs<clang::CXXOperatorCallExpr>("lvalue");
            auto lit = result.Nodes.getNodeAs<clang::StringLiteral>("lit");
            const ValueDecl *decl = nullptr;
            if (auto ref = result.Nodes.getNodeAs<clang::MemberExpr>("ci")) {
                decl = ref->getMemberDecl();
            } else if (auto ref = result.Nodes.getNodeAs<clang::DeclRefExpr>("ci")) {
                decl = ref->getDecl();
            }
            if (decl == nullptr) {
                std::cerr << "ERROR: couldn't match assignment to declaration\n";
                result.Nodes.getNodeAs<clang::Expr>("ci")->dumpColor();
                ::exit(1);
            }
            channel_producer_map[(void *)decl] = lit->getString().str();
            std::cout << "Matched " << lit->getString().str() << " to "<< (void *)decl << std::endl;
        }
    };
    ChannelAssignment assignment{channels, channel_producer_map};

    struct ChannelRef : MatchFinder::MatchCallback
    {
        ChannelMap &channels;
        ChannelNodeMap &channel_producer_map;
        ChannelRef(ChannelMap &c_, ChannelNodeMap &cp_) : channels(c_), channel_producer_map(cp_) {}

        void run(const MatchFinder::MatchResult &result) override
        {
            auto add_ref = [&](auto ref, auto decl, auto timing) {
                // ref->dumpColor();
                auto loc = result.Context->getFullLoc(ref->getLocStart());
                if (channel_producer_map.find((void *)decl) == channel_producer_map.end()) {
                    auto &sm = result.Context->getSourceManager();
                    std::cout << "WARNING: found ref " << (void *)decl << " with no source at "
                              << sm.getFilename(loc).str() << ":" << loc.getSpellingLineNumber()
                              << std::endl;
                    return;
                }
                auto func = find_func(ref, result.Context);
                if (func == nullptr) {
                    std::cerr << "WARNING: could not find context for ref ";
                    auto &sm = result.Context->getSourceManager();
                    std::cerr << "at " << sm.getFilename(loc).str() << ":"
                              << loc.getSpellingLineNumber() << std::endl;
                    return;
                }
                auto ref_func = find_proper_name(find_func(ref, result.Context), result.Context);
                auto channel_name = channel_producer_map[(void *)decl];
                auto &c = channels[channel_name];
                c.refs.insert(Channel::Ref{ref_func, timing});
                std::cout << "found ref " << channel_name << " - " << ref_func << ":" << loc.getSpellingLineNumber() << ":" << timing << std::endl;
            };

            std::string timing = "direct";
            if (auto callee = result.Nodes.getNodeAs<FunctionDecl>("callee")) {
                timing = callee->getNameAsString();
            } else if (auto callee = result.Nodes.getNodeAs<UnresolvedLookupExpr>("callee")) {
                timing = callee->getName().getAsString();
            }
            
            auto add_lookup_direct = [&, this] (const clang::CXXMemberCallExpr *ref) {
                auto lit = find_string_literal(ref);
                std::cout << "adding to producer map " << lit->getString().str() << ": "
                          << (void *)ref->getRecordDecl() << std::endl;
                channel_producer_map[(void *)ref->getRecordDecl()] = lit->getString().str();
                add_ref(ref, ref->getRecordDecl(), timing);
            };

            auto expr = result.Nodes.getNodeAs<clang::Expr>("ref");
            if (auto ref = llvm::dyn_cast<clang::DeclRefExpr>(expr)) {
                add_ref(ref, ref->getDecl(), timing);
            } else if (auto ref = llvm::dyn_cast<clang::MemberExpr>(expr)) {
                add_ref(ref, ref->getMemberDecl(), timing);
            } else if (auto ref = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr)) {
                // this is probably a lookup + call all in one. Add the channel.
                // TODO: but will this hit on a sched call too?
                // adding the channel if it's not there already.
                add_lookup_direct(ref);
            } else if (auto ref = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr)) {
                // this too is a lookup + call all in one
                for (auto i = ref->child_begin(); i != ref->child_end(); ++i) {
                    if (auto cxxmce = llvm::dyn_cast<clang::CXXMemberCallExpr>(*i)) {
                        add_lookup_direct(cxxmce);
                        break;
                    }
                }
            } else {
                std::cout << "WARNING: couldn't cast expr\n";
                std::cout << "\t: "
                          << result.Context->getFullLoc(expr->getLocStart()).getSpellingLineNumber()
                          << std::endl
                          << "\t: " << QualType(expr->getType()).getAsString()
                          << std::endl;
                expr->dumpColor();
            }
        }
    };
    ChannelRef ref{channels, channel_producer_map};

    struct ChannelHook : MatchFinder::MatchCallback
    {
        ChannelMap &channels;
        ChannelNodeMap &channel_producer_map;
        std::map<const CXXMethodDecl *, ProperName> &channel_consumer_map;
        ChannelHook(ChannelMap &c_, ChannelNodeMap &cp_,
                    std::map<const CXXMethodDecl *, ProperName> &cc_)
            : channels(c_), channel_producer_map(cp_), channel_consumer_map(cc_)
        {
        }

        void run(const MatchFinder::MatchResult &result) override
        {
            #if 0
            auto call = result.Nodes.getNodeAs<clang::CXXMemberCallExpr>("call");
            call->dumpColor();

            for (auto &p : result.Nodes.getMap()) {
                std::cout << p.first << (void *)&p.second << std::endl;
            }
            #endif

            auto hook = result.Nodes.getNodeAs<clang::CallExpr>("hook");

            std::string cn;
            if (auto ref = result.Nodes.getNodeAs<clang::MemberExpr>("ref")) {
                auto key = (void *)ref->getMemberDecl();
                if (channel_producer_map.find(key) == channel_producer_map.end()) {
                    std::cout << "WARNING: could not find producer for hook ref\n";
                    return;
                }
                cn = channel_producer_map[key];
            } else if (auto ref = result.Nodes.getNodeAs<clang::DeclRefExpr>("ref")) {
                auto key = (void *)ref->getDecl();
                if (channel_producer_map.find(key) == channel_producer_map.end()) {
                    std::cout << "WARNING: could not find producer for hook ref\n";
                    return;
                }
                cn = channel_producer_map[key];
            } else if (auto lit = result.Nodes.getNodeAs<clang::StringLiteral>("lit")) {
                cn = lit->getString().str();
            } else {
                std::cout << "WARNING: could not find channel source\n";
                hook->dumpColor();
                return;
            }
            auto &c = channels[cn];
            auto dest_func = find_hook_target(result);
            if (dest_func == nullptr) {
                auto loc = result.Context->getFullLoc(hook->getLocStart());
                std::cout << "ERROR: unknown hook type @ " << loc.getSpellingLineNumber()
                          << std::endl;
                hook->dumpColor();
                return;
            }
            auto name = find_proper_name(dest_func, result.Context);
            if (const auto method = llvm::dyn_cast<CXXMethodDecl>(dest_func)) {
                channel_consumer_map[method] = name;
            }
            c.dests.insert(name);
            std::cout << "found hook at " << name << std::endl;
        }
    };
    ChannelHook hook{channels, channel_producer_map, channel_consumer_map};

    struct ChannelUnique : MatchFinder::MatchCallback
    {
        ChannelMap &channels;
        ChannelNodeMap &channel_producer_map;
        std::map<const CXXMethodDecl *, ProperName> &channel_consumer_map;
        ChannelUnique(ChannelMap &c_, ChannelNodeMap &cp_,
                      std::map<const CXXMethodDecl *, ProperName> &cc_)
            : channels(c_), channel_producer_map(cp_), channel_consumer_map(cc_)
        {
        }

        void run(const MatchFinder::MatchResult &result) override
        {
            auto hook = result.Nodes.getNodeAs<clang::CallExpr>("hook");
            if (hook->getType()->isDependentType()) return;
            auto dest_func = find_hook_target(result);
            auto ref = result.Nodes.getNodeAs<clang::MemberExpr>("channel");
            auto channel_name = channel_producer_map[(void *)ref->getMemberDecl()];
            auto &c = channels[channel_name];
            auto dest_name = find_proper_name(dest_func, result.Context);
            c.dests.insert(dest_name);
            std::cout << "found unique " << channel_name << " -> " << dest_name << std::endl;

            if (const auto method = llvm::dyn_cast<CXXMethodDecl>(dest_func)) {
                channel_consumer_map[method] = dest_name;
            }
#if 0
            // raw_os_ostream os(std::cout);
            for (int i = 0; i < hook->getNumArgs(); ++i) {
                std::cout << i << ":  " << std::endl;
                hook->getArg(i)->dumpColor();
                // hook->getArg(i)->printPretty(os, nullptr, result.Context->getPrintingPolicy());
            }
#endif
        }
    };
    ChannelUnique unique{channels, channel_producer_map, channel_consumer_map};

    MatchFinder matcher;
    MatchFinder prod_matcher;

public:
    AnalyzeFrontendConsumer()
    {
        auto ci = hasType(cxxRecordDecl(hasName("::conduit::ChannelInterface")));
        auto lit = cxxConstructExpr(hasDescendant(stringLiteral().bind("lit")));
        prod_matcher.addMatcher(
            cxxOperatorCallExpr(hasArgument(0, memberExpr(ci).bind("ci")),
                                hasArgument(1, hasDescendant(lit)), hasOverloadedOperatorName("="))
                .bind("lvalue"),
            &assignment);
        prod_matcher.addMatcher(
            cxxOperatorCallExpr(hasArgument(0, declRefExpr(ci).bind("ci")),
                                hasArgument(1, hasDescendant(lit)), hasOverloadedOperatorName("="))
                .bind("lvalue"),
            &assignment);
        prod_matcher.addMatcher(
            fieldDecl(ci, hasDescendant(cxxConstructExpr(hasDescendant(stringLiteral().bind("lit")))))
                .bind("ci"),
            &decl);
        prod_matcher.addMatcher(
            varDecl(ci, hasDescendant(cxxConstructExpr(hasDescendant(stringLiteral().bind("lit")))))
                .bind("ci"),
            &decl);

#if 0
        matcher.addMatcher(
            callExpr(isExpansionInMainFile(),
                     has(ignoringParenImpCasts(
                         expr(hasType(cxxRecordDecl(hasName("ChannelInterface"))))
                             .bind("ref"))),
                     unless(callee(cxxMethodDecl(hasName("operator=")))))
                .bind("timing"),
            &ref);
    matcher.addMatcher(
        callExpr(isExpansionInMainFile(), has(ignoringParenImpCasts(expr(ci).bind("ref"))),
                 unless(callee(cxxMethodDecl(hasName("operator=")))))
            .bind("timing"),
        &ref);
#endif

        // sched calls
        matcher.addMatcher(callExpr(isExpansionInMainFile(),
                                    hasArgument(2, ignoringParenImpCasts(expr(ci).bind("ref"))),
                                    callee(functionDecl(hasName("sched")).bind("callee")))
                               .bind("timing"),
                           &ref);
        matcher.addMatcher(callExpr(isExpansionInMainFile(),
                                    hasArgument(2, ignoringParenImpCasts(expr(ci).bind("ref"))),
                                    callee(unresolvedLookupExpr(hasAnyDeclaration(namedDecl(hasUnderlyingDecl(hasName("sched"))))).bind("callee")))
                               .bind("timing"),
                           &ref);
        // cycle only sched calls
        matcher.addMatcher(callExpr(isExpansionInMainFile(),
                                    hasArgument(1, ignoringParenImpCasts(expr(ci).bind("ref"))),
                                    callee(functionDecl(hasName("sched")).bind("callee")))
                               .bind("timing"),
                           &ref);
        matcher.addMatcher(callExpr(isExpansionInMainFile(),
                                    hasArgument(1, ignoringParenImpCasts(expr(ci).bind("ref"))),
                                    callee(unresolvedLookupExpr(hasAnyDeclaration(namedDecl(hasUnderlyingDecl(hasName("sched"))))).bind("callee")))
                               .bind("timing"),
                           &ref);
        // direct calls
        // worked for template:
        // 
        // match callExpr(isExpansionInMainFile(), callee(varDecl(hasType(cxxRecordDecl(hasName("Foo"))))))
        matcher.addMatcher(callExpr(isExpansionInMainFile(),
                                    hasArgument(0, declRefExpr().bind("ref")),
                                    callee(varDecl(ci)))
                               .bind("timing"),
                           &ref);
        matcher.addMatcher(callExpr(isExpansionInMainFile(),
                                    hasArgument(0, ignoringParenImpCasts(expr(ci).bind("ref"))),
                                    callee(cxxMethodDecl(hasName("operator()"))))
                               .bind("timing"),
                           &ref);

        // hook handling
        auto hook_target
            = anyOf(lambdaExpr().bind("lambda"),
                    declRefExpr().bind("func"),
                    callExpr(callee(functionDecl(hasName("make_binder"))),
                             hasArgument(1, hasDescendant(declRefExpr().bind("bound_method")))));


        // match ci.hook
        matcher.addMatcher(cxxMemberCallExpr(isExpansionInMainFile(),
                                             on(declRefExpr(ci).bind("ref")),
                                             callee(cxxMethodDecl(hasName("hook"))),
                                             hasArgument(0, expr(hook_target))).bind("hook"),
                           &hook);
        // matcher.addMatcher(cxxMemberCallExpr(isExpansionInMainFile(),
                                             // on(memberExpr(ci).bind("ref")),
                                             // callee(cxxMethodDecl(hasName("hook"))),
                                             // hasArgument(0, hasDescendant(expr(hook_target)))).bind("hook"),
                           // &hook);


        auto channel_lookup = cxxMemberCallExpr(on(hasType(cxxRecordDecl(hasName("::conduit::Registrar")))),
                                                callee(cxxMethodDecl(hasName("lookup"))),
                                                hasArgument(0, hasDescendant(stringLiteral().bind("lit"))));
        // match reg.lookup().hook
        matcher.addMatcher(
            cxxMemberCallExpr(
                isExpansionInMainFile(),
                on(ci),
                callee(cxxMethodDecl(hasName("hook"))),
                callee(memberExpr(hasDescendant(channel_lookup))),
                hasAnyArgument(hasDescendant(expr(hook_target)))).bind("hook"),
            &hook);

        // unique handling
        matcher.addMatcher(
            callExpr(isExpansionInMainFile(), callee(functionDecl(hasName("unique"))),
                     hasArgument(0, hook_target),
                     hasArgument(1, hasDescendant(memberExpr(hasType(cxxRecordDecl(
                                                                 hasName("ChannelInterface"))))
                                                      .bind("channel"))))
                .bind("hook"),
            &unique);
    }

    void find_hook_ref(const CXXMethodDecl *decl, const ProperName &pn, ASTContext &context)
    {
        struct CallTree : MatchFinder::MatchCallback
        {
            std::set<const CXXMethodDecl *> &input_calls;
            std::set<const CXXMethodDecl *> found_calls;
            std::string decl_context;
            std::string indent;
            CallTree(std::set<const CXXMethodDecl *> &c_, std::string dc_, std::string i_)
                : input_calls(c_), decl_context(std::move(dc_)), indent(i_)
            {
            }

            void run(const MatchFinder::MatchResult &result) override
            {
                auto call = result.Nodes.getNodeAs<CallExpr>("call");
                if (call->getCalleeDecl() == nullptr) {
                    // std::cout << "null callee " << to_string(call, result.Context) << std::endl;
                    return;
                }
                auto method = llvm::dyn_cast<CXXMethodDecl>(call->getCalleeDecl());
                if (method && (input_calls.find(method) == input_calls.end())) {
                    std::string method_context
                        = get_method_context(method)->getQualifiedNameAsString();
#if 0
                    {
                        auto loc = result.Context->getFullLoc(method->getLocStart());
                        std::cout << "found a call " << method->getNameAsString() << ":" << loc.getSpellingLineNumber() << std::endl;
                        std::cout << "method_context " << method_context << " decl_context " << decl_context << std::endl;
                    }
#endif

                    if (method_context == decl_context) {
                        auto loc = result.Context->getFullLoc(method->getLocStart());
                        std::cout << indent << "found method_context " << method_context << "-"
                                  << method->getNameAsString() << ":" << loc.getSpellingLineNumber()
                                  << std::endl;
                        found_calls.insert(method);
                    }
                }
            }
        };

        auto decl_context = get_method_context(decl)->getQualifiedNameAsString();
        std::set<const CXXMethodDecl *> calls;

        std::function<void(const CXXMethodDecl *, std::string)> find_calls
            = [&](const CXXMethodDecl *decl, std::string indent) {
                  calls.insert(decl);
                  if (decl_context.empty()) {
                      auto loc = context.getFullLoc(decl->getLocStart());
                      auto &sm = context.getSourceManager();
                      std::cout << "decl_context empty for " << sm.getFilename(loc).str() << ":"
                                << loc.getSpellingLineNumber() << " - " << to_string(decl, &context)
                                << std::endl;
                      return;
                  }
                  CallTree call_tree{calls, decl_context, indent};
                  MatchFinder call_matcher;
                  call_matcher.addMatcher(cxxMethodDecl(forEachDescendant(callExpr().bind("call"))),
                                          &call_tree);
                  call_matcher.match(*decl, context);
                  for (auto new_decl : call_tree.found_calls)
                      find_calls(new_decl, indent + "  ");
              };
        find_calls(decl, "");

        struct MemberExpr : MatchFinder::MatchCallback
        {
            const ProperName &proper_name;
            MemberExpr(const ProperName &pn) : proper_name(pn) {}

            void run(const MatchFinder::MatchResult &result) override
            {
                auto method = result.Nodes.getNodeAs<CXXMethodDecl>("method");
                auto mem = result.Nodes.getNodeAs<clang::MemberExpr>("mem");
                auto fd = llvm::dyn_cast<FieldDecl>(mem->getMemberDecl());

                auto loc = result.Context->getFullLoc(mem->getLocStart());
                if (!fd || fd->getType().isConstQualified()) {
                    // std::cout << "not a field decl\n";
                    // std::cout << "\t" << loc.getSpellingLineNumber() << std::endl;
                    return;
                }

                if (!method->getParent()->isLambda() && !fd->isMutable() && method->isConst()) {
                    auto loc = result.Context->getFullLoc(method->getLocStart());
                    std::cout << "ignoring const method " << method->getQualifiedNameAsString()
                              << " : " << loc.getSpellingLineNumber() << std::endl;
                    return;
                }

                // only interested in member variable accesses
                std::string method_context = get_method_context(method)->getNameAsString();
                std::string fd_context = fd->getParent()->getNameAsString();

                if (method_context != fd_context) {
                    // std::cout << "ignoring mismatch: " << method_context << " != " << fd_context
                    // << std::endl;
                    // std::cout << "\t" << loc.getSpellingLineNumber() << std::endl;
                    return;
                }
                std::cout << "found memberExpr: \"" << fd->getNameAsString() << "\"\n";
                std::cout << "\t from " << fd_context << ":" << loc.getSpellingLineNumber()
                          << std::endl;
                hook_vars[proper_name].insert(fd->getNameAsString());
            }
        };

        for (auto decl : calls) {
            MemberExpr member_expr{pn};
            MatchFinder member_matcher;
            auto ci = hasType(cxxRecordDecl(hasName("ChannelInterface")));
            member_matcher.addMatcher(
                cxxMethodDecl(anyOf(forEachDescendant(memberExpr(unless(ci)).bind("mem")),
                                    forEachDescendant(cxxMemberCallExpr(unless(ci)).bind("mem"))))
                    .bind("method"),
                &member_expr);

            member_matcher.match(*decl, context);
        }
    }

    void HandleTranslationUnit(ASTContext &context) override
    {
        channel_producer_map = ChannelNodeMap{};
        channel_consumer_map = decltype(channel_consumer_map){};
        prod_matcher.matchAST(context);
        matcher.matchAST(context);

        for (auto &p : channel_consumer_map) {
            find_hook_ref(p.first, p.second, context);
        }
    }
};

class AnalyzeFrontendAction : public ASTFrontendAction
{
public:
    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef file) override
    {
        return llvm::make_unique<AnalyzeFrontendConsumer>();
    }
};

int main(int argc, const char **argv)
{
    CommonOptionsParser options_parser(argc, argv, AnalyzeToolCategory);
    ClangTool tool(options_parser.getCompilations(), options_parser.getSourcePathList());
    tool.appendArgumentsAdjuster(getClangStripOutputAdjuster());
    tool.appendArgumentsAdjuster(getClangSyntaxOnlyAdjuster());
#ifdef _MSC_VER
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-fms-compatibility", ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-fms-compatibility-version=19", ArgumentInsertPosition::END));
    // tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fno-ms-compatibliity",
    // ArgumentInsertPosition::END));
    // tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fno-delayed-template-parsing",
    // ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-D NOMINMAX", ArgumentInsertPosition::END));
    // tool.appendArgumentsAdjuster(getInsertArgumentAdjuster("-fms-extensions",
    // ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-Wno-nonportable-include-path", ArgumentInsertPosition::END));
#else
    // tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
        // "-v", ArgumentInsertPosition::END));
#endif
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-Wno-error", ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-Wno-unused-private-field", ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-Wno-unused-variable", ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-Wno-unused-function", ArgumentInsertPosition::END));
    tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("-D USE_FIXVEC_POOL", ArgumentInsertPosition::END));

    auto ret = tool.run(newFrontendActionFactory<AnalyzeFrontendAction>().get());
    if (ret) return ret;

    // move hook_vars data into channels
    if (state_check.getValue()) {
        for (auto &hvp : hook_vars) {
            auto &hook_pn = hvp.first;
            // std::cout << "hook_var " << hook_pn << std::endl;
            for (auto &cp : channels) {
                auto &channel = cp.second;
                auto i = channel.dests.find(hook_pn);
                if (i != channel.dests.end()) {
                    auto &context = const_cast<std::set<std::string> &>(i->context);
                    std::copy(hvp.second.begin(), hvp.second.end(),
                              std::inserter(context, context.end()));
                    std::cout << cp.first << " -- " << i->name << " : {" << ::join(i->context, ",")
                              << "}\n";
                }
            }
        }
    }

    std::set<ProperName> entities;
    for (auto &p : channels) {
        auto &c = p.second;
        std::cout << "found channel: " << p.first << std::endl;
        for (auto &d : c.dests)
            std::cout << "\t-> " << d << " (" << d.context.size() << ")\n";
        for (auto &r : c.refs)
            std::cout << "\t" << r.name << ":" << r.timing << " ->\n";
    }
    for (auto &p : channels) {
        auto &c = p.second;
        std::for_each(c.dests.begin(), c.dests.end(),
                      [&](auto &d) { auto p = entities.insert(d); });
    }
    for (auto &p : channels) {
        auto &c = p.second;
        std::transform(c.refs.begin(), c.refs.end(), std::inserter(entities, entities.begin()),
                       [](auto &ref) { return ref.name; });
    }
    std::map<ProperName, int> entity_map;
    int counter = 0;
    for (auto &e : entities) {
        entity_map[e] = ++counter;
    }

    // colorize the entities if enabled by the color option.
    if (seed.getValue() == -1) {
        std::random_device rd;
        seed = rd();
    }
    if (colorize.getValue()) {
        std::cout << "random color seed " << seed << std::endl;
    }
    std::mt19937 gen(seed.getValue());
    std::uniform_int_distribution<int> dist(0, 255);
    struct Color
    {
        int r, g, b;
        Color operator+(Color o) const { return Color{r + o.r, g + o.g, b + o.b}; }
        Color operator-(Color o) const { return Color{r - o.r, g - o.g, b - o.b}; }
        Color operator/(int d) const { return Color{r / d, g / d, b / d}; }
        Color normalize() const
        {
            return Color{std::max(r, 0) & 0xff, std::max(g, 0) & 0xff, std::max(b, 0) & 0xff};
        }
        int intensity() const { return int(r * 0.299 + g * 0.587 + b * 0.114); }
        operator int() const
        {
            auto n = normalize();
            return (n.r << 16) | (n.g << 8) | b;
        }
        std::string str() const
        {
            std::ostringstream s;
            s << "{" << r << ", " << g << ", " << b << "}";
            return s.str();
        }
    } base;
    const auto bases = std::vector<Color>{
        {0x07, 0x36, 0x42}, {0x07, 0x88, 0x9b}, {0x00, 0x2b, 0x36}, {0x37, 0x37, 0x37},
        {0x96, 0x85, 0x8f}, {0x6d, 0x79, 0x93}, {0x0e, 0x0b, 0x16}, {0xff, 0xff, 0xff},
    };
    std::vector<int> base_indices(entity_map.size());
    std::iota(base_indices.begin(), base_indices.end(), 0);
    std::shuffle(base_indices.begin(), base_indices.end(), gen);
    auto base_iter = base_indices.begin();

    std::map<std::string, Color> unit_colors;
    std::for_each(entity_map.begin(), entity_map.end(), [&](auto &e) {
        if (unit_colors.find(e.first.unit) == unit_colors.end()) {
            if (colorize.getValue()) {
                auto rand = Color{dist(gen), dist(gen), dist(gen)};
                auto base = *(bases.begin() + (*base_iter++ % bases.size()));
                unit_colors[e.first.unit] = (base + rand) / 2;
            } else {
                unit_colors[e.first.unit] = {0x55, 0x55, 0x55};
            }
        }
    });

    std::ofstream ofs(output_filename.getValue(), std::ios::binary | std::ios::trunc);

    ofs << "digraph html {\n";
    ofs << "\t// splines=\"ortho\";\n";
    ofs << "\tgraph [rankdir=\"LR\", sep=\"1.00\", ranksep=\"1.00\", nodesep=\"1.00\", "
           "arrowsize=\"3.0\"];\n";
    ofs << "\tforcelabels=true;\n";
    ofs << "\toverlap=false;\n";
    auto prev = ofs.fill('0');
    for (auto &p : entity_map) {
        auto bg = unit_colors[p.first.unit];
        auto fg = bg.intensity() > 186 ? Color{0, 0, 0} : Color{0xff, 0xff, 0xff};
        ofs << "\t" << p.second << " [label=\"" << p.first << "\""
            << "fontsize=\"30\", style=filled, "
            << " fillcolor=\"#" << std::hex << std::setw(6) << static_cast<int>(bg)
            << "\", fontcolor=\"#" << std::setw(6) << static_cast<int>(fg) << "\", shape=box];\n"
            << std::dec;
    }
    ofs.fill(prev);

    for (auto &c : channels) {
        for (auto &ref : c.second.refs) {
            for (auto &dest : c.second.dests) {
                ofs << "\t" << entity_map[ref.name] << " -> " << entity_map[dest] << " [label=\""
                    << c.first << "\n"
                    << ref.timing << "\", fontsize=\"30\", fontcolor=\"blue\"];\n";
            }
        }
    }
    for (auto &c : channels) {
        if (c.second.refs.empty() && !c.second.dests.empty()) {
            entity_map[c.first] = ++counter;
            ofs << "\t" << entity_map[c.first] << " [label=\"" << c.first << "\""
                << "fontsize=\"30\", style=filled, fillcolor=\"#e0e0e0\", fontcolor=\"blue\", "
                   "shape=box];\n";
            for (auto &dest : c.second.dests) {
                ofs << "\t" << entity_map[c.first] << " -> " << entity_map[dest] << ";\n";
            }
        }
    }
    for (auto &c : channels) {
        if (!c.second.refs.empty() && c.second.dests.empty()) {
            entity_map[c.first] = ++counter;
            ofs << "\t" << entity_map[c.first] << " [label=\"" << c.first << "\""
                << "fontsize=\"30\", style=filled, fillcolor=\"#eeeeee\", fontcolor=\"blue\", "
                   "shape=box];\n";
            for (auto &ref : c.second.refs) {
                ofs << "\t" << entity_map[ref.name] << " -> " << entity_map[c.first]
                    << " [label=\"" << ref.timing << "\", fontsize=\"30\"];\n";
            }
        }
    }
    ofs << "}\n";
}
