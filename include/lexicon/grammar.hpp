// Context free grammar of the question forms, loaded from a text file.
//
// A terminal of this grammar is not a word but a part of speech together with an
// optional feature constraint, so the grammar never mentions a concrete word and
// the lexicon can grow without touching it.
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lexicon/features.hpp"
#include "lexicon/morph.hpp"

namespace lex {

struct TerminalSpec {
    std::string pos;
    Features constraint;  // values may offer alternatives: vform=base|past
};

struct Rule {
    int lhs = -1;
    std::vector<int> rhs;
    std::string action;  // name of the semantic rule that goes with this production
};

class Grammar {
public:
    static Grammar load(const std::string& path);

    int symbol(std::string_view name) const;
    const std::string& name(int symbol) const { return names_[static_cast<std::size_t>(symbol)]; }
    bool is_terminal(int symbol) const { return terminals_.count(symbol) != 0; }
    const TerminalSpec& terminal(int symbol) const { return terminals_.at(symbol); }

    // True when the reading can stand under this terminal symbol.
    bool accepts(int symbol, const Reading& reading) const;

    const std::vector<Rule>& rules() const { return rules_; }
    const std::vector<int>& rules_for(int lhs) const;
    int start() const { return start_; }
    std::size_t symbol_count() const { return names_.size(); }
    std::string rule_text(int rule) const;

private:
    int intern(std::string_view name);

    std::vector<std::string> names_;
    std::unordered_map<std::string, int> ids_;
    std::unordered_map<int, TerminalSpec> terminals_;
    std::vector<Rule> rules_;
    std::vector<std::vector<int>> by_lhs_;
    int start_ = -1;
};

}  // namespace lex
