// The logical form: a small typed query algebra.
//
// A query is a projection over a formula. A formula is a conjunction of atoms,
// possibly under negation or under a universal quantifier. Atoms are typed: a
// unary atom is a type membership, Film(x), and a binary atom is a relation of
// the knowledge model, directed(p, f). Nothing in this file knows about SQL,
// SPARQL or English, which is the point: three consumers read the same form.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace lex {

struct Term {
    enum class Kind { Variable, Entity, Number };

    Kind kind = Kind::Variable;
    int var = -1;
    std::string entity;
    long long number = 0;

    static Term variable(int id);
    static Term entity_ref(std::string id);
    static Term numeric(long long value);
    std::string str() const;
    bool operator==(const Term& other) const;
};

struct Atom {
    std::string relation;
    std::vector<Term> args;
    std::string str() const;
};

struct Formula;
using FormulaRef = std::shared_ptr<const Formula>;

struct Formula {
    enum class Kind { Atom, And, Not, Forall };

    Kind kind = Kind::Atom;
    Atom atom;
    std::vector<FormulaRef> subs;
    int var = -1;  // Forall: subs[0] restricts this variable, subs[1] is the body
};

FormulaRef make_atom(Atom atom);
FormulaRef make_and(std::vector<FormulaRef> parts);
FormulaRef make_not(FormulaRef sub);
FormulaRef make_forall(int var, FormulaRef restriction, FormulaRef body);
std::string to_string(const FormulaRef& formula);

// Every variable that occurs anywhere in the formula, in order of appearance.
std::vector<int> variables_of(const FormulaRef& formula);

struct Query {
    std::vector<int> result_vars;
    bool count = false;    // "how many"
    bool boolean = false;  // yes or no question
    FormulaRef body;
    std::map<int, std::string> var_types;
    std::map<int, std::string> var_labels;  // the noun that introduced the variable

    std::string str() const;

    // The same string for two readings that mean the same thing. Variable
    // numbers are reassigned by order of appearance and the conjuncts are
    // sorted, so a difference in the parse tree that makes no difference to the
    // meaning does not survive into the key.
    std::string canonical() const;
};

}  // namespace lex
