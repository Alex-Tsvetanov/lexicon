#include "lexicon/synth.hpp"

#include <algorithm>
#include <set>

namespace lex {
namespace {

void flatten(const FormulaRef& formula, std::vector<FormulaRef>& parts) {
    if (!formula) return;
    if (formula->kind == Formula::Kind::And) {
        for (const FormulaRef& sub : formula->subs) flatten(sub, parts);
        return;
    }
    parts.push_back(formula);
}

bool mentions(const FormulaRef& formula, int var) {
    if (!formula) return false;
    if (formula->kind == Formula::Kind::Atom) {
        for (const Term& term : formula->atom.args)
            if (term.kind == Term::Kind::Variable && term.var == var) return true;
        return false;
    }
    if (formula->kind == Formula::Kind::Forall && formula->var == var) return true;
    for (const FormulaRef& sub : formula->subs)
        if (mentions(sub, var)) return true;
    return false;
}

std::string join(const std::vector<std::string>& items, const std::string& separator) {
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty()) out += separator;
        out += item;
    }
    return out;
}

bool is_clause(const std::string& text) { return text.rfind("that ", 0) == 0; }

class Realiser {
public:
    Realiser(const Schema& schema, const Morphology& morph, const KnowledgeBase& kb,
             const Query& query)
        : schema_(schema), morph_(morph), kb_(kb), query_(query) {
        flatten(query_.body, parts_);
        used_.assign(parts_.size(), false);
    }

    std::string run() {
        if (query_.boolean) return "You are asking whether " + clause_list() + ".";
        if (query_.result_vars.empty()) return "You are asking something with nothing to report.";
        const int result = query_.result_vars.front();
        const std::string phrase = describe(result);
        if (query_.count) return "You are asking for the number of " + phrase + ".";
        return "You are asking for the " + phrase + ".";
    }

private:
    const Schema& schema_;
    const Morphology& morph_;
    const KnowledgeBase& kb_;
    const Query& query_;
    std::vector<FormulaRef> parts_;
    std::vector<bool> used_;
    std::set<int> busy_;
    // Inside a quantified phrase the prepositional wording reads better, and it
    // is also the wording the question itself used.
    bool prefer_prepositional_ = false;

    std::string type_of(int var) const {
        const auto found = query_.var_types.find(var);
        return found == query_.var_types.end() ? std::string() : found->second;
    }

    // The first noun of the lexicon that denotes this type, used when a variable
    // was introduced by a grammar rule rather than by a word.
    std::string noun_for_type(const std::string& type) const {
        for (const LexEntry& entry : morph_.entries())
            if (entry.pos == "noun" && entry.sem.has("type", type)) return entry.lemma;
        return type;
    }

    std::string plural(const std::string& lemma) const {
        const auto form = morph_.generate(lemma, "noun", Features::parse("num=pl"));
        return form.has_value() ? *form : lemma + "s";
    }

    // The verb of a relation, inflected by the transducer running backwards. A
    // verb whose object arrives through a preposition, "act in", cannot be used
    // in the passive, so the caller can ask for transitive verbs only.
    std::string verb_form(const std::string& relation, const std::string& want,
                          bool transitive_only) const {
        for (const LexEntry& entry : morph_.entries()) {
            if (entry.pos != "verb" || !entry.sem.has("rel", relation)) continue;
            if (transitive_only && entry.sem.get("objprep") != nullptr) continue;
            const auto form = morph_.generate(entry.lemma, "verb", Features::parse(want));
            if (form.has_value()) return *form;
        }
        return {};
    }

    const PrepFrame* frame_for(const std::string& relation, const std::string& attach_type,
                               bool attach_is_first) const {
        for (const PrepFrame& frame : schema_.frames()) {
            if (frame.relation != relation || frame.attach_type != attach_type) continue;
            if (frame.arg_first == attach_is_first) continue;
            return &frame;
        }
        return nullptr;
    }

    std::string term_text(const Term& term) {
        switch (term.kind) {
            case Term::Kind::Number: return std::to_string(term.number);
            case Term::Kind::Entity: {
                const std::string* label = kb_.label_of(term.entity);
                return label != nullptr ? *label : term.entity;
            }
            case Term::Kind::Variable: return describe(term.var);
        }
        return "something";
    }

    std::string head_noun(int var) const {
        const auto found = query_.var_labels.find(var);
        std::string lemma = found == query_.var_labels.end() ? std::string() : found->second;
        if (lemma.empty() || schema_.type(lemma) != nullptr) lemma = noun_for_type(type_of(var));
        return plural(lemma);
    }

    // One atom, said as a modifier of `var`.
    std::string modifier(const Atom& atom, int var, bool negated) {
        if (atom.args.size() != 2) return {};
        const bool var_first = atom.args[0].kind == Term::Kind::Variable && atom.args[0].var == var;
        const Term& other = var_first ? atom.args[1] : atom.args[0];
        const PrepFrame* frame = frame_for(atom.relation, type_of(var), var_first);
        const std::string negation = negated ? "not " : "";

        if (frame != nullptr && !negated && prefer_prepositional_)
            return frame->prep + " " + term_text(other);
        if (!var_first) {
            const std::string participle = verb_form(atom.relation, "vform=part", true);
            if (!participle.empty())
                return "that were " + negation + participle + " by " + term_text(other);
        }
        if (var_first && !negated) {
            const std::string past = verb_form(atom.relation, "vform=past", false);
            if (!past.empty())
                return "that " + past + (frame != nullptr ? " " + frame->prep : "") + " " +
                       term_text(other);
        }
        if (frame != nullptr && !negated) return frame->prep + " " + term_text(other);
        if (frame != nullptr) return "that are " + negation + frame->prep + " " + term_text(other);
        return "that stand in the relation " + atom.relation + " to " + term_text(other);
    }

    // "that acted in every film by Christopher Nolan", and the same shape with
    // "no" for a negated existential.
    std::string quantified_clause(int var, const Atom& link, const std::vector<FormulaRef>& rest,
                                  const std::string& quantifier) {
        if (link.args.size() != 2) return {};
        const bool var_first = link.args[0].kind == Term::Kind::Variable && link.args[0].var == var;
        const Term& other = var_first ? link.args[1] : link.args[0];
        if (other.kind != Term::Kind::Variable) return {};
        const PrepFrame* frame = frame_for(link.relation, type_of(var), var_first);
        const std::string phrase = quantifier + " " + describe_restricted(other.var, rest);
        const std::string past = verb_form(link.relation, "vform=past", false);
        if (!past.empty() && var_first)
            return "that " + past + (frame != nullptr ? " " + frame->prep : "") + " " + phrase;
        if (frame != nullptr) return "that are " + frame->prep + " " + phrase;
        return "that stand in the relation " + link.relation + " to " + phrase;
    }

    std::string describe(int var) {
        if (busy_.count(var) != 0) return head_noun(var);
        busy_.insert(var);

        std::vector<std::string> prepositional;
        std::vector<std::string> clauses;
        for (std::size_t i = 0; i < parts_.size(); ++i) {
            if (used_[i] || !mentions(parts_[i], var)) continue;
            const FormulaRef part = parts_[i];
            used_[i] = true;

            if (part->kind == Formula::Kind::Atom) {
                if (part->atom.args.size() == 1) continue;  // the head noun says the type
                const std::string text = modifier(part->atom, var, false);
                if (!text.empty()) (is_clause(text) ? clauses : prepositional).push_back(text);
                continue;
            }

            if (part->kind == Formula::Kind::Not) {
                std::vector<FormulaRef> inner;
                flatten(part->subs.at(0), inner);
                std::vector<FormulaRef> about;
                std::vector<FormulaRef> rest;
                for (const FormulaRef& sub : inner)
                    (mentions(sub, var) ? about : rest).push_back(sub);
                for (const FormulaRef& sub : about) {
                    if (sub->kind != Formula::Kind::Atom) continue;
                    const std::string text = rest.empty()
                                                 ? modifier(sub->atom, var, true)
                                                 : quantified_clause(var, sub->atom, rest, "no");
                    if (!text.empty()) (is_clause(text) ? clauses : prepositional).push_back(text);
                }
                continue;
            }

            if (part->kind == Formula::Kind::Forall) {
                std::vector<FormulaRef> body;
                std::vector<FormulaRef> restriction;
                flatten(part->subs.at(1), body);
                flatten(part->subs.at(0), restriction);
                for (const FormulaRef& sub : body) {
                    if (sub->kind != Formula::Kind::Atom || !mentions(sub, var)) continue;
                    const std::string text = quantified_clause(var, sub->atom, restriction, "every");
                    if (!text.empty()) clauses.push_back(text);
                }
            }
        }
        busy_.erase(var);

        std::string out = head_noun(var);
        if (!prepositional.empty()) out += " " + join(prepositional, " and ");
        if (!clauses.empty()) out += " " + join(clauses, " and ");
        return out;
    }

    // The head of a quantified phrase, said with its own restriction only.
    std::string describe_restricted(int var, const std::vector<FormulaRef>& restriction) {
        const std::vector<FormulaRef> saved_parts = parts_;
        const std::vector<bool> saved_used = used_;
        const bool saved_preference = prefer_prepositional_;
        parts_ = restriction;
        used_.assign(parts_.size(), false);
        prefer_prepositional_ = true;
        std::string out = describe(var);
        prefer_prepositional_ = saved_preference;

        // A quantified phrase reads better in the singular: "every film", not
        // "every films".
        const auto found = query_.var_labels.find(var);
        const std::string lemma =
            found == query_.var_labels.end() ? noun_for_type(type_of(var)) : found->second;
        const std::string plural_head = plural(lemma);
        if (out.rfind(plural_head, 0) == 0) out = lemma + out.substr(plural_head.size());

        parts_ = saved_parts;
        used_ = saved_used;
        return out;
    }

    // A yes or no question has no variable to describe, so its atoms are said as
    // plain sentences.
    std::string clause_list() {
        std::vector<std::string> said;
        for (std::size_t i = 0; i < parts_.size(); ++i) {
            if (parts_[i]->kind != Formula::Kind::Atom) continue;
            const Atom& atom = parts_[i]->atom;
            if (atom.args.size() != 2) continue;
            used_[i] = true;
            const std::string past = verb_form(atom.relation, "vform=past", false);
            if (!past.empty()) {
                said.push_back(term_text(atom.args[0]) + " " + past + " " + term_text(atom.args[1]));
            } else {
                said.push_back(term_text(atom.args[0]) + " stands in the relation " +
                               atom.relation + " to " + term_text(atom.args[1]));
            }
        }
        return said.empty() ? "that holds" : join(said, " and ");
    }
};

}  // namespace

std::string paraphrase(const Schema& schema, const Morphology& morph, const KnowledgeBase& kb,
                       const Query& query) {
    return Realiser(schema, morph, kb, query).run();
}

}  // namespace lex
