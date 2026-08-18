#include "lexicon/kb.hpp"

#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>

#include "lexicon/features.hpp"

namespace lex {
namespace {

bool looks_numeric(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
               return c >= '0' && c <= '9';
           });
}

Value value_from(const std::string& text) {
    Value value;
    if (looks_numeric(text)) {
        value.numeric = true;
        value.number = std::stoll(text);
    } else {
        value.id = text;
    }
    return value;
}

// A binding of query variables to knowledge base values.
using Bindings = std::map<int, Value>;

bool bind_term(const Term& term, const Value& value, Bindings& bindings, std::vector<int>& added) {
    switch (term.kind) {
        case Term::Kind::Variable: {
            const auto found = bindings.find(term.var);
            if (found != bindings.end()) return found->second == value;
            bindings.emplace(term.var, value);
            added.push_back(term.var);
            return true;
        }
        case Term::Kind::Entity:
            return !value.numeric && value.id == term.entity;
        case Term::Kind::Number:
            return value.numeric && value.number == term.number;
    }
    return false;
}

}  // namespace

std::string Value::str() const { return numeric ? std::to_string(number) : id; }

bool Value::operator==(const Value& other) const {
    return numeric == other.numeric && number == other.number && id == other.id;
}

bool Value::operator<(const Value& other) const {
    if (numeric != other.numeric) return numeric;
    if (numeric) return number < other.number;
    return id < other.id;
}

std::size_t KnowledgeBase::fact_count() const {
    std::size_t total = 0;
    for (const auto& relation : relations_) total += relation.second.size();
    return total;
}

const std::string* KnowledgeBase::type_of(const std::string& entity) const {
    const auto found = entity_types_.find(entity);
    return found == entity_types_.end() ? nullptr : &found->second;
}

const std::string* KnowledgeBase::label_of(const std::string& entity) const {
    const auto found = entity_labels_.find(entity);
    return found == entity_labels_.end() ? nullptr : &found->second;
}

const std::vector<std::string>& KnowledgeBase::entities_of_type(const std::string& type) const {
    static const std::vector<std::string> empty;
    const auto found = by_type_.find(type);
    return found == by_type_.end() ? empty : found->second;
}

const std::vector<std::vector<Value>>* KnowledgeBase::extension(const std::string& relation) const {
    const auto found = relations_.find(relation);
    return found == relations_.end() ? nullptr : &found->second;
}

KnowledgeBase KnowledgeBase::load(const std::string& path) {
    KnowledgeBase kb;
    for (const std::string& line : read_config_lines(path)) {
        const std::vector<std::string> parts = split_ws(line);
        if (parts[0] == "ENTITY") {
            if (parts.size() < 3) throw std::runtime_error("ENTITY needs a type and an id: " + line);
            const std::string& type = parts[1];
            const std::string& id = parts[2];
            std::string label = id;
            const std::size_t quote = line.find('"');
            if (quote != std::string::npos) {
                const std::size_t close = line.find('"', quote + 1);
                if (close == std::string::npos) throw std::runtime_error("unclosed label: " + line);
                label = line.substr(quote + 1, close - quote - 1);
            }
            kb.entity_types_.emplace(id, type);
            kb.entity_labels_.emplace(id, label);
            kb.by_type_[type].push_back(id);
        } else if (parts[0] == "FACT") {
            if (parts.size() < 3) throw std::runtime_error("FACT needs a relation and arguments: " + line);
            std::vector<Value> tuple;
            for (std::size_t i = 2; i < parts.size(); ++i) tuple.push_back(value_from(parts[i]));
            kb.relations_[parts[1]].push_back(std::move(tuple));
        } else {
            throw std::runtime_error("unknown directive in the knowledge base: " + line);
        }
    }
    return kb;
}

std::string Answer::str() const {
    if (is_boolean) return boolean_value ? "yes" : "no";
    if (is_count) return std::to_string(count);
    if (rows.empty()) return "(no rows)";
    std::string out;
    for (const std::string& row : rows) {
        if (!out.empty()) out += ", ";
        out += row;
    }
    return out;
}

Answer evaluate(const KnowledgeBase& kb, const Query& query) {
    // Conjuncts are reordered so that positive atoms bind the variables before
    // negation and universal quantification test them. Without that order a
    // negation would be asked about a variable nothing has bound yet.
    std::function<void(const FormulaRef&, std::vector<FormulaRef>&)> flatten =
        [&](const FormulaRef& formula, std::vector<FormulaRef>& parts) {
            if (!formula) return;
            if (formula->kind == Formula::Kind::And) {
                for (const FormulaRef& sub : formula->subs) flatten(sub, parts);
                return;
            }
            parts.push_back(formula);
        };

    std::function<bool(const std::vector<FormulaRef>&, std::size_t, Bindings&,
                       const std::function<bool(const Bindings&)>&)>
        solve_all;
    std::function<bool(const FormulaRef&, Bindings&, const std::function<bool(const Bindings&)>&)>
        solve;

    // The callback returns true to ask for more solutions and false to stop.
    solve = [&](const FormulaRef& formula, Bindings& bindings,
                const std::function<bool(const Bindings&)>& yield) -> bool {
        if (!formula) return yield(bindings);
        switch (formula->kind) {
            case Formula::Kind::Atom: {
                const Atom& atom = formula->atom;
                if (atom.args.size() == 1) {
                    // A unary atom is a type membership.
                    for (const std::string& id : kb.entities_of_type(atom.relation)) {
                        Value value;
                        value.id = id;
                        std::vector<int> added;
                        if (bind_term(atom.args[0], value, bindings, added)) {
                            if (!yield(bindings)) {
                                for (const int var : added) bindings.erase(var);
                                return false;
                            }
                        }
                        for (const int var : added) bindings.erase(var);
                    }
                    return true;
                }
                const std::vector<std::vector<Value>>* tuples = kb.extension(atom.relation);
                if (tuples == nullptr) return true;
                for (const std::vector<Value>& tuple : *tuples) {
                    if (tuple.size() != atom.args.size()) continue;
                    std::vector<int> added;
                    bool ok = true;
                    for (std::size_t i = 0; i < tuple.size() && ok; ++i)
                        ok = bind_term(atom.args[i], tuple[i], bindings, added);
                    const bool keep_going = !ok || yield(bindings);
                    for (const int var : added) bindings.erase(var);
                    if (!keep_going) return false;
                }
                return true;
            }
            case Formula::Kind::And: {
                std::vector<FormulaRef> parts;
                flatten(formula, parts);
                std::stable_sort(parts.begin(), parts.end(),
                                 [](const FormulaRef& a, const FormulaRef& b) {
                                     auto rank = [](const FormulaRef& f) {
                                         if (f->kind == Formula::Kind::Atom) return 0;
                                         if (f->kind == Formula::Kind::Forall) return 1;
                                         return 2;
                                     };
                                     return rank(a) < rank(b);
                                 });
                return solve_all(parts, 0, bindings, yield);
            }
            case Formula::Kind::Not: {
                bool found = false;
                Bindings copy = bindings;
                solve(formula->subs.at(0), copy, [&](const Bindings&) {
                    found = true;
                    return false;
                });
                return found ? true : yield(bindings);
            }
            case Formula::Kind::Forall: {
                bool holds = true;
                Bindings copy = bindings;
                solve(formula->subs.at(0), copy, [&](const Bindings& restricted) {
                    Bindings inner = restricted;
                    bool witness = false;
                    solve(formula->subs.at(1), inner, [&](const Bindings&) {
                        witness = true;
                        return false;
                    });
                    if (!witness) holds = false;
                    return holds;
                });
                return holds ? yield(bindings) : true;
            }
        }
        return true;
    };

    solve_all = [&](const std::vector<FormulaRef>& parts, std::size_t index, Bindings& bindings,
                    const std::function<bool(const Bindings&)>& yield) -> bool {
        if (index == parts.size()) return yield(bindings);
        return solve(parts[index], bindings, [&](const Bindings&) {
            return solve_all(parts, index + 1, bindings, yield);
        });
    };

    Answer answer;
    answer.is_boolean = query.boolean;
    answer.is_count = query.count;

    std::set<Value> distinct;
    Bindings bindings;
    solve(query.body, bindings, [&](const Bindings& solution) {
        if (query.boolean) {
            answer.boolean_value = true;
            return false;
        }
        if (query.result_vars.empty()) return false;
        const auto found = solution.find(query.result_vars.front());
        if (found != solution.end()) distinct.insert(found->second);
        return true;
    });

    answer.count = static_cast<long long>(distinct.size());
    for (const Value& value : distinct) {
        const std::string* label = value.numeric ? nullptr : kb.label_of(value.id);
        answer.rows.push_back(label != nullptr ? *label : value.str());
    }
    std::sort(answer.rows.begin(), answer.rows.end());
    return answer;
}

}  // namespace lex
