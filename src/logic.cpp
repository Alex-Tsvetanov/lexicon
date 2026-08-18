#include "lexicon/logic.hpp"

#include <algorithm>
#include <functional>

namespace lex {
namespace {

void collect_variables(const FormulaRef& formula, std::vector<int>& seen) {
    if (!formula) return;
    auto note = [&seen](int var) {
        if (var >= 0 && std::find(seen.begin(), seen.end(), var) == seen.end()) seen.push_back(var);
    };
    if (formula->kind == Formula::Kind::Atom) {
        for (const Term& term : formula->atom.args)
            if (term.kind == Term::Kind::Variable) note(term.var);
        return;
    }
    if (formula->kind == Formula::Kind::Forall) note(formula->var);
    for (const FormulaRef& sub : formula->subs) collect_variables(sub, seen);
}

// Renders a formula with the variables renamed through `naming`.
std::string render(const FormulaRef& formula, const std::map<int, int>& naming) {
    if (!formula) return "true";
    auto name_of = [&naming](const Term& term) {
        if (term.kind != Term::Kind::Variable) return term.str();
        const auto found = naming.find(term.var);
        return std::string("x") + std::to_string(found == naming.end() ? term.var : found->second);
    };
    switch (formula->kind) {
        case Formula::Kind::Atom: {
            std::string out = formula->atom.relation + "(";
            for (std::size_t i = 0; i < formula->atom.args.size(); ++i) {
                if (i > 0) out += ", ";
                out += name_of(formula->atom.args[i]);
            }
            return out + ")";
        }
        case Formula::Kind::Not:
            return "not " + render(formula->subs.at(0), naming);
        case Formula::Kind::Forall: {
            const auto found = naming.find(formula->var);
            const std::string var =
                "x" + std::to_string(found == naming.end() ? formula->var : found->second);
            return "forall " + var + " [" + render(formula->subs.at(0), naming) + "] " +
                   render(formula->subs.at(1), naming);
        }
        case Formula::Kind::And: {
            std::string out;
            for (const FormulaRef& sub : formula->subs) {
                if (!out.empty()) out += " & ";
                out += render(sub, naming);
            }
            return out.empty() ? "true" : out;
        }
    }
    return "true";
}

// Splits a conjunction into its parts, so the canonical form can sort them.
void flatten(const FormulaRef& formula, std::vector<FormulaRef>& parts) {
    if (!formula) return;
    if (formula->kind == Formula::Kind::And) {
        for (const FormulaRef& sub : formula->subs) flatten(sub, parts);
        return;
    }
    parts.push_back(formula);
}

}  // namespace

Term Term::variable(int id) {
    Term term;
    term.kind = Kind::Variable;
    term.var = id;
    return term;
}

Term Term::entity_ref(std::string id) {
    Term term;
    term.kind = Kind::Entity;
    term.entity = std::move(id);
    return term;
}

Term Term::numeric(long long value) {
    Term term;
    term.kind = Kind::Number;
    term.number = value;
    return term;
}

std::string Term::str() const {
    switch (kind) {
        case Kind::Variable: return "x" + std::to_string(var);
        case Kind::Entity: return entity;
        case Kind::Number: return std::to_string(number);
    }
    return "?";
}

bool Term::operator==(const Term& other) const {
    return kind == other.kind && var == other.var && entity == other.entity &&
           number == other.number;
}

std::string Atom::str() const {
    std::string out = relation + "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out += ", ";
        out += args[i].str();
    }
    return out + ")";
}

FormulaRef make_atom(Atom atom) {
    auto formula = std::make_shared<Formula>();
    formula->kind = Formula::Kind::Atom;
    formula->atom = std::move(atom);
    return formula;
}

FormulaRef make_and(std::vector<FormulaRef> parts) {
    std::vector<FormulaRef> kept;
    for (FormulaRef& part : parts)
        if (part) kept.push_back(std::move(part));
    if (kept.size() == 1) return kept.front();
    auto formula = std::make_shared<Formula>();
    formula->kind = Formula::Kind::And;
    formula->subs = std::move(kept);
    return formula;
}

FormulaRef make_not(FormulaRef sub) {
    auto formula = std::make_shared<Formula>();
    formula->kind = Formula::Kind::Not;
    formula->subs.push_back(std::move(sub));
    return formula;
}

FormulaRef make_forall(int var, FormulaRef restriction, FormulaRef body) {
    auto formula = std::make_shared<Formula>();
    formula->kind = Formula::Kind::Forall;
    formula->var = var;
    formula->subs.push_back(std::move(restriction));
    formula->subs.push_back(std::move(body));
    return formula;
}

std::string to_string(const FormulaRef& formula) { return render(formula, {}); }

std::vector<int> variables_of(const FormulaRef& formula) {
    std::vector<int> seen;
    collect_variables(formula, seen);
    return seen;
}

std::string Query::str() const {
    std::string head;
    if (boolean) {
        head = "ask";
    } else if (count) {
        head = "count";
    } else {
        head = "select";
    }
    for (const int var : result_vars) head += " x" + std::to_string(var);
    return head + " where " + to_string(body);
}

std::string Query::canonical() const {
    std::map<int, int> naming;
    for (const int var : result_vars)
        if (naming.find(var) == naming.end()) naming.emplace(var, static_cast<int>(naming.size()));
    for (const int var : variables_of(body))
        if (naming.find(var) == naming.end()) naming.emplace(var, static_cast<int>(naming.size()));

    std::vector<FormulaRef> parts;
    flatten(body, parts);
    std::vector<std::string> rendered;
    rendered.reserve(parts.size());
    for (const FormulaRef& part : parts) rendered.push_back(render(part, naming));
    std::sort(rendered.begin(), rendered.end());

    std::string head = boolean ? "ask" : (count ? "count" : "select");
    for (const int var : result_vars) head += " x" + std::to_string(naming.at(var));
    std::string out = head + " where";
    for (const std::string& part : rendered) out += " " + part + ";";
    return out;
}

}  // namespace lex
