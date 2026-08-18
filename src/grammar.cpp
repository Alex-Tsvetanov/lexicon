#include "lexicon/grammar.hpp"

#include <stdexcept>

namespace lex {

int Grammar::intern(std::string_view name) {
    const std::string key(name);
    const auto found = ids_.find(key);
    if (found != ids_.end()) return found->second;
    names_.push_back(key);
    const int id = static_cast<int>(names_.size()) - 1;
    ids_.emplace(key, id);
    by_lhs_.emplace_back();
    return id;
}

int Grammar::symbol(std::string_view name) const {
    const auto found = ids_.find(std::string(name));
    return found == ids_.end() ? -1 : found->second;
}

const std::vector<int>& Grammar::rules_for(int lhs) const {
    static const std::vector<int> empty;
    if (lhs < 0 || static_cast<std::size_t>(lhs) >= by_lhs_.size()) return empty;
    return by_lhs_[static_cast<std::size_t>(lhs)];
}

bool Grammar::accepts(int symbol, const Reading& reading) const {
    const auto found = terminals_.find(symbol);
    if (found == terminals_.end()) return false;
    if (found->second.pos != reading.pos) return false;
    return reading.feats.satisfies(found->second.constraint);
}

std::string Grammar::rule_text(int rule) const {
    const Rule& r = rules_.at(static_cast<std::size_t>(rule));
    std::string out = name(r.lhs) + " ->";
    for (const int symbol : r.rhs) out += " " + name(symbol);
    return out;
}

Grammar Grammar::load(const std::string& path) {
    Grammar grammar;
    std::string start_name;

    for (const std::string& line : read_config_lines(path)) {
        const std::vector<std::string> parts = split_ws(line);
        if (parts[0] == "%start") {
            if (parts.size() != 2) throw std::runtime_error("%start needs one symbol: " + line);
            start_name = parts[1];
        } else if (parts[0] == "%terminal") {
            if (parts.size() < 3) throw std::runtime_error("%terminal needs a name and a part of speech: " + line);
            TerminalSpec spec;
            spec.pos = parts[2];
            std::string rest;
            for (std::size_t i = 3; i < parts.size(); ++i) rest += parts[i] + " ";
            spec.constraint = Features::parse(rest);
            grammar.terminals_.emplace(grammar.intern(parts[1]), std::move(spec));
        } else {
            // LHS -> A B C @action
            if (parts.size() < 3 || parts[1] != "->")
                throw std::runtime_error("expected a production: " + line);
            Rule rule;
            rule.lhs = grammar.intern(parts[0]);
            for (std::size_t i = 2; i < parts.size(); ++i) {
                if (parts[i][0] == '@') {
                    rule.action = parts[i].substr(1);
                    break;
                }
                rule.rhs.push_back(grammar.intern(parts[i]));
            }
            if (rule.rhs.empty())
                throw std::runtime_error("empty productions are not allowed: " + line);
            if (rule.action.empty())
                throw std::runtime_error("production without a semantic rule: " + line);
            grammar.rules_.push_back(std::move(rule));
            const Rule& stored = grammar.rules_.back();
            grammar.by_lhs_[static_cast<std::size_t>(stored.lhs)].push_back(
                static_cast<int>(grammar.rules_.size()) - 1);
        }
    }

    if (start_name.empty()) throw std::runtime_error("grammar has no %start symbol");
    grammar.start_ = grammar.symbol(start_name);
    if (grammar.start_ < 0) throw std::runtime_error("start symbol never appears: " + start_name);

    for (const Rule& rule : grammar.rules_) {
        for (const int symbol : rule.rhs) {
            if (grammar.is_terminal(symbol)) continue;
            if (grammar.rules_for(symbol).empty())
                throw std::runtime_error("symbol " + grammar.name(symbol) +
                                         " is neither a terminal nor the head of a production");
        }
    }
    return grammar;
}

}  // namespace lex
