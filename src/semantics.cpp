#include "lexicon/semantics.hpp"

#include <algorithm>
#include <stdexcept>

namespace lex {
namespace {

// Thrown when a reading cannot be mapped onto the knowledge model. It is not an
// error of the program: rejecting readings is the job of this layer.
struct Rejected : std::runtime_error {
    explicit Rejected(const std::string& why) : std::runtime_error(why) {}
};

// The meaning of one node of the parse tree. One structure covers the three
// kinds of phrase the grammar builds, because they are combined by the same
// small set of rules and a variant would only add ceremony.
struct Sem {
    enum class Kind { Phrase, Prep, Verb };

    Kind kind = Kind::Phrase;

    // Noun phrase.
    Term term;
    std::string type;
    std::string quant = "exists";  // exists, forall or none
    std::vector<Atom> atoms;
    bool questioned = false;
    std::string label;
    int head_token = -1;

    // Prepositional phrase.
    std::string prep;
    std::vector<Sem> complement;

    // Verb phrase.
    std::string relation;
    std::string objprep;
    std::vector<Sem> object;   // at most one
    std::vector<Sem> pending;  // prepositional phrases not yet attached
};

}  // namespace

const TypeSpec* Schema::type(const std::string& name) const {
    for (const TypeSpec& spec : types_)
        if (spec.name == name) return &spec;
    return nullptr;
}

const RelationSpec* Schema::relation(const std::string& name) const {
    for (const RelationSpec& spec : relations_)
        if (spec.name == name) return &spec;
    return nullptr;
}

const PrepFrame* Schema::frame(const std::string& prep, const std::string& arg_type,
                               const std::string& attach_type) const {
    for (const PrepFrame& spec : frames_)
        if (spec.prep == prep && spec.arg_type == arg_type && spec.attach_type == attach_type)
            return &spec;
    return nullptr;
}

Schema Schema::load(const std::string& path) {
    auto field = [](const std::vector<std::string>& parts, const std::string& key) -> std::string {
        for (const std::string& part : parts) {
            if (part.rfind(key + "=", 0) == 0) return part.substr(key.size() + 1);
        }
        return {};
    };
    auto csv = [](const std::string& text) {
        std::vector<std::string> items;
        std::size_t begin = 0;
        while (begin <= text.size()) {
            const std::size_t comma = text.find(',', begin);
            const std::size_t end = comma == std::string::npos ? text.size() : comma;
            items.push_back(text.substr(begin, end - begin));
            if (comma == std::string::npos) break;
            begin = comma + 1;
        }
        return items;
    };

    Schema schema;
    for (const std::string& line : read_config_lines(path)) {
        const std::vector<std::string> parts = split_ws(line);
        if (parts[0] == "TYPE") {
            TypeSpec spec;
            spec.name = parts.at(1);
            spec.scalar = !field(parts, "scalar").empty();
            spec.table = field(parts, "table");
            spec.key = field(parts, "key");
            spec.label = field(parts, "label");
            spec.iri = field(parts, "iri");
            schema.types_.push_back(std::move(spec));
        } else if (parts[0] == "REL") {
            RelationSpec spec;
            spec.name = parts.at(1);
            spec.args = csv(field(parts, "args"));
            spec.store = field(parts, "store");
            spec.table = field(parts, "table");
            spec.cols = csv(field(parts, "cols"));
            spec.iri = field(parts, "iri");
            schema.relations_.push_back(std::move(spec));
        } else if (parts[0] == "FRAME") {
            PrepFrame spec;
            spec.prep = parts.at(1);
            spec.arg_type = field(parts, "arg");
            spec.attach_type = field(parts, "attach");
            spec.relation = field(parts, "rel");
            spec.arg_first = field(parts, "order").rfind("arg", 0) == 0;
            schema.frames_.push_back(std::move(spec));
        } else {
            throw std::runtime_error("unknown directive in the schema: " + line);
        }
    }
    return schema;
}

// ---------------------------------------------------------------------------
// Composition
// ---------------------------------------------------------------------------

namespace {

class Composer {
public:
    Composer(const Grammar& grammar, const Morphology& morph, const Schema& schema,
             const KnowledgeBase& kb, const std::vector<Token>& tokens)
        : grammar_(grammar), morph_(morph), schema_(schema), kb_(kb), tokens_(tokens) {}

    Query run(const Tree& tree) {
        const Sem clause = build(tree);
        if (clause.kind != Sem::Kind::Phrase) throw Rejected("the tree does not build a clause");
        std::vector<FormulaRef> parts = pack(clause.atoms);
        if (parts.empty()) throw Rejected("the clause says nothing");
        Query query = query_;
        query.body = make_and(std::move(parts));
        return query;
    }

    int cost() const { return cost_; }

private:
    const Grammar& grammar_;
    const Morphology& morph_;
    const Schema& schema_;
    const KnowledgeBase& kb_;
    const std::vector<Token>& tokens_;
    Query query_;
    int next_var_ = 0;
    int cost_ = 0;
    std::vector<FormulaRef> scoped_;  // quantified subformulas built during assembly

    const Reading& reading_of(const Tree& leaf) const {
        return tokens_.at(static_cast<std::size_t>(leaf.token))
            .readings.at(static_cast<std::size_t>(leaf.reading));
    }

    const Features* lexical_sem(const Tree& leaf) const {
        const Reading& reading = reading_of(leaf);
        const LexEntry* entry = morph_.entry(reading.entry);
        if (entry == nullptr)
            throw Rejected("the word " + tokens_.at(static_cast<std::size_t>(leaf.token)).text +
                           " is not in the lexicon");
        return &entry->sem;
    }

    std::string field(const Tree& leaf, const std::string& key) const {
        const std::string* value = lexical_sem(leaf)->get(key);
        return value == nullptr ? std::string() : *value;
    }

    int fresh(const std::string& type, const std::string& label) {
        const int var = next_var_++;
        query_.var_types[var] = type;
        query_.var_labels[var] = label;
        return var;
    }

    std::vector<FormulaRef> pack(const std::vector<Atom>& atoms) const {
        std::vector<FormulaRef> parts = scoped_;
        for (const Atom& atom : atoms) parts.push_back(make_atom(atom));
        return parts;
    }

    const std::string& action(const Tree& tree) const {
        return grammar_.rules().at(static_cast<std::size_t>(tree.rule)).action;
    }

    // -- attachment ---------------------------------------------------------

    // Turns a prepositional phrase into an atom about `target`, or rejects the
    // reading when the knowledge model has no frame for that combination.
    void attach(const Sem& pp, Sem& target) {
        const Sem& arg = pp.complement.at(0);
        const PrepFrame* frame = schema_.frame(pp.prep, arg.type, target.type);
        if (frame == nullptr)
            throw Rejected("no relation for \"" + pp.prep + "\" between " + target.type + " and " +
                           arg.type);
        Atom atom;
        atom.relation = frame->relation;
        if (frame->arg_first) {
            atom.args = {arg.term, target.term};
        } else {
            atom.args = {target.term, arg.term};
        }
        target.atoms.insert(target.atoms.end(), arg.atoms.begin(), arg.atoms.end());
        target.atoms.push_back(std::move(atom));
        if (pp.head_token >= 0 && target.head_token >= 0) cost_ += pp.head_token - target.head_token;
        absorb_quantifier(arg, target.atoms);
    }

    // A quantified noun phrase is lifted out of the conjunction it sits in.
    void absorb_quantifier(const Sem& phrase, std::vector<Atom>& atoms) {
        if (phrase.quant == "exists" || phrase.term.kind != Term::Kind::Variable) return;
        const int var = phrase.term.var;

        std::vector<Atom> restriction = phrase.atoms;
        std::vector<Atom> body;
        std::vector<Atom> rest;
        for (const Atom& atom : atoms) {
            const bool in_restriction =
                std::find_if(restriction.begin(), restriction.end(), [&](const Atom& other) {
                    return other.str() == atom.str();
                }) != restriction.end();
            const bool mentions =
                std::find_if(atom.args.begin(), atom.args.end(), [&](const Term& term) {
                    return term.kind == Term::Kind::Variable && term.var == var;
                }) != atom.args.end();
            if (in_restriction) continue;
            if (mentions) {
                body.push_back(atom);
            } else {
                rest.push_back(atom);
            }
        }
        atoms = rest;

        std::vector<FormulaRef> restriction_parts;
        for (const Atom& atom : restriction) restriction_parts.push_back(make_atom(atom));
        std::vector<FormulaRef> body_parts;
        for (const Atom& atom : body) body_parts.push_back(make_atom(atom));

        if (phrase.quant == "forall") {
            if (body_parts.empty()) throw Rejected("a universal noun phrase with nothing to say");
            scoped_.push_back(
                make_forall(var, make_and(restriction_parts), make_and(body_parts)));
        } else {  // "no film", a negated existential
            std::vector<FormulaRef> all = restriction_parts;
            all.insert(all.end(), body_parts.begin(), body_parts.end());
            scoped_.push_back(make_not(make_and(all)));
        }
    }

    // -- clause assembly ----------------------------------------------------

    struct ClauseParts {
        Sem questioned;
        bool has_questioned = false;
        std::vector<Sem> subject;  // at most one
        Sem verb;
        bool negated = false;
        bool passive = false;
        bool count = false;
        bool boolean = false;
    };

    Sem assemble(ClauseParts parts) {
        const RelationSpec* relation = schema_.relation(parts.verb.relation);
        if (relation == nullptr || relation->args.size() != 2)
            throw Rejected("the verb does not denote a binary relation of the model");

        Sem subject;
        Sem object;
        bool have_subject = false;
        bool have_object = false;

        if (parts.passive) {
            // The by phrase of a passive fills the subject slot rather than
            // adding a relation of its own.
            for (std::size_t i = 0; i < parts.verb.pending.size(); ++i) {
                const Sem& pp = parts.verb.pending[i];
                if (pp.prep != "by") continue;
                if (pp.complement.at(0).type != relation->args[0]) continue;
                subject = pp.complement.at(0);
                have_subject = true;
                parts.verb.pending.erase(parts.verb.pending.begin() + static_cast<long>(i));
                break;
            }
            if (!have_subject) {
                subject.term = Term::variable(fresh(relation->args[0], relation->args[0]));
                subject.type = relation->args[0];
                have_subject = true;
            }
        } else if (!parts.subject.empty()) {
            subject = parts.subject.front();
            have_subject = true;
        }

        if (!parts.verb.object.empty()) {
            object = parts.verb.object.front();
            have_object = true;
        } else if (!parts.verb.objprep.empty()) {
            // "acted in <film>": the object arrives through a preposition named
            // by the verb, so that phrase is taken before generic attachment.
            for (std::size_t i = 0; i < parts.verb.pending.size(); ++i) {
                const Sem& pp = parts.verb.pending[i];
                if (pp.prep != parts.verb.objprep) continue;
                if (pp.complement.at(0).type != relation->args[1]) continue;
                object = pp.complement.at(0);
                have_object = true;
                parts.verb.pending.erase(parts.verb.pending.begin() + static_cast<long>(i));
                break;
            }
        }

        // The questioned phrase fills whichever slot is still open.
        if (parts.has_questioned) {
            if (!have_subject && !parts.passive) {
                subject = parts.questioned;
                have_subject = true;
            } else if (!have_object) {
                object = parts.questioned;
                have_object = true;
            } else {
                throw Rejected("the question word has no slot in the clause");
            }
        }
        if (!have_subject || !have_object)
            throw Rejected("the clause leaves an argument of the relation unfilled");

        auto settle_type = [&](Sem& phrase, const std::string& expected) {
            if (phrase.type.empty()) {
                // A bare question word takes its type from the argument slot.
                phrase.type = expected;
                if (phrase.term.kind == Term::Kind::Variable) {
                    query_.var_types[phrase.term.var] = expected;
                    query_.var_labels[phrase.term.var] = expected;
                    Atom type_atom;
                    type_atom.relation = expected;
                    type_atom.args = {phrase.term};
                    phrase.atoms.insert(phrase.atoms.begin(), std::move(type_atom));
                }
                return;
            }
            if (phrase.type != expected)
                throw Rejected("the relation " + relation->name + " wants " + expected +
                               " where the sentence gives " + phrase.type);
        };
        settle_type(subject, relation->args[0]);
        settle_type(object, relation->args[1]);

        Atom verb_atom;
        verb_atom.relation = relation->name;
        verb_atom.args = {subject.term, object.term};

        // Everything the verb phrase contributes, which is what a negation
        // covers. The restrictions of the noun phrases stay outside it.
        // The referent a verb phrase modifier restricts is the object, but the
        // site it attaches to is the verb. Measuring the distance from the verb
        // is what makes the lower attachment, inside the noun phrase, the
        // preferred one when both readings survive.
        Sem inner;
        inner.type = object.type;
        inner.term = object.term;
        inner.head_token = parts.verb.head_token;
        inner.atoms.push_back(std::move(verb_atom));
        for (const Sem& pp : parts.verb.pending) attach(pp, inner);

        std::vector<Atom> outer = subject.atoms;
        outer.insert(outer.end(), object.atoms.begin(), object.atoms.end());
        if (parts.negated) {
            std::vector<FormulaRef> negated;
            for (const Atom& atom : inner.atoms) negated.push_back(make_atom(atom));
            scoped_.push_back(make_not(make_and(negated)));
        } else {
            outer.insert(outer.end(), inner.atoms.begin(), inner.atoms.end());
        }
        // The quantifiers are lifted after the verb has had its say, so that the
        // body of a universal contains the relation the sentence is about.
        absorb_quantifier(subject, outer);
        absorb_quantifier(object, outer);

        query_.count = parts.count;
        query_.boolean = parts.boolean;
        if (!parts.boolean && parts.has_questioned) {
            if (parts.questioned.term.kind != Term::Kind::Variable)
                throw Rejected("the questioned phrase is not a variable");
            query_.result_vars = {parts.questioned.term.var};
        }

        Sem clause;
        clause.atoms = outer;
        return clause;
    }

    // -- the recursive walk -------------------------------------------------

    Sem verb_from_leaf(const Tree& leaf) {
        Sem sem;
        sem.kind = Sem::Kind::Verb;
        sem.relation = field(leaf, "rel");
        sem.objprep = field(leaf, "objprep");
        sem.head_token = leaf.token;
        if (sem.relation.empty()) throw Rejected("the verb denotes no relation of the model");
        return sem;
    }

    Sem build(const Tree& tree) {
        if (tree.rule < 0) throw Rejected("a leaf reached the composer directly");
        const std::string& rule = action(tree);
        const std::vector<Tree>& child = tree.children;

        if (rule == "pass") return build(child.at(0));

        if (rule == "nom_n") {
            const Tree& leaf = child.at(0);
            const std::string type = field(leaf, "type");
            if (type.empty()) throw Rejected("the noun denotes no type of the model");
            Sem sem;
            sem.label = reading_of(leaf).lemma;
            sem.term = Term::variable(fresh(type, sem.label));
            sem.type = type;
            sem.head_token = leaf.token;
            Atom atom;
            atom.relation = type;
            atom.args = {sem.term};
            sem.atoms.push_back(std::move(atom));
            return sem;
        }

        if (rule == "nom_pp") {
            Sem nom = build(child.at(0));
            const Sem pp = build(child.at(1));
            attach(pp, nom);
            return nom;
        }

        if (rule == "np_pn") {
            const Tree& leaf = child.at(0);
            const std::string id = field(leaf, "ent");
            if (id.empty()) throw Rejected("the proper noun denotes no entity");
            const std::string* type = kb_.type_of(id);
            if (type == nullptr) throw Rejected("the entity " + id + " is not in the knowledge base");
            Sem sem;
            sem.term = Term::entity_ref(id);
            sem.type = *type;
            sem.label = reading_of(leaf).lemma;
            sem.head_token = leaf.token;
            return sem;
        }

        if (rule == "np_num") {
            const Tree& leaf = child.at(0);
            Sem sem;
            sem.term = Term::numeric(std::stoll(reading_of(leaf).lemma));
            sem.type = "Year";
            sem.label = reading_of(leaf).lemma;
            sem.head_token = leaf.token;
            return sem;
        }

        if (rule == "np_bare") return build(child.at(0));

        if (rule == "np_det") {
            Sem nom = build(child.at(1));
            const std::string quantifier = field(child.at(0), "det");
            if (quantifier.empty()) throw Rejected("the determiner introduces no quantifier");
            nom.quant = quantifier;
            return nom;
        }

        if (rule == "pp") {
            Sem sem;
            sem.kind = Sem::Kind::Prep;
            sem.prep = field(child.at(0), "prep");
            if (sem.prep.empty()) throw Rejected("the preposition has no entry in the lexicon");
            sem.head_token = child.at(0).token;
            sem.complement.push_back(build(child.at(1)));
            return sem;
        }

        if (rule == "vp_v" || rule == "vpass_v") return verb_from_leaf(child.at(0));

        if (rule == "vp_v_np") {
            Sem sem = verb_from_leaf(child.at(0));
            sem.object.push_back(build(child.at(1)));
            return sem;
        }

        if (rule == "vp_pp" || rule == "vpass_pp") {
            Sem verb = build(child.at(0));
            verb.pending.push_back(build(child.at(1)));
            return verb;
        }

        if (rule == "wh_bare") {
            const Tree& leaf = child.at(0);
            const std::string type = field(leaf, "type");
            Sem sem;
            sem.questioned = true;
            sem.label = type.empty() ? std::string("thing") : type;
            sem.term = Term::variable(fresh(type, sem.label));
            sem.type = type;
            sem.head_token = leaf.token;
            if (!type.empty()) {
                Atom atom;
                atom.relation = type;
                atom.args = {sem.term};
                sem.atoms.push_back(std::move(atom));
            }
            return sem;
        }

        if (rule == "wh_nom") {
            Sem nom = build(child.at(1));
            nom.questioned = true;
            return nom;
        }

        if (rule == "howmany_np") {
            Sem nom = build(child.at(2));
            nom.questioned = true;
            return nom;
        }

        // Clause level rules.
        ClauseParts parts;
        if (rule == "wh_do" || rule == "wh_do_neg") {
            parts.questioned = build(child.at(0));
            parts.has_questioned = true;
            parts.subject.push_back(build(child.at(2)));
            parts.negated = rule == "wh_do_neg";
            parts.verb = build(child.at(parts.negated ? 4 : 3));
        } else if (rule == "wh_subj") {
            parts.questioned = build(child.at(0));
            parts.has_questioned = true;
            parts.verb = build(child.at(1));
        } else if (rule == "wh_pass" || rule == "wh_pass_neg") {
            parts.questioned = build(child.at(0));
            parts.has_questioned = true;
            parts.passive = true;
            parts.negated = rule == "wh_pass_neg";
            parts.verb = build(child.at(parts.negated ? 3 : 2));
        } else if (rule == "howmany_do") {
            parts.questioned = build(child.at(0));
            parts.has_questioned = true;
            parts.count = true;
            parts.subject.push_back(build(child.at(2)));
            parts.verb = build(child.at(3));
        } else if (rule == "howmany_subj") {
            parts.questioned = build(child.at(0));
            parts.has_questioned = true;
            parts.count = true;
            parts.verb = build(child.at(1));
        } else if (rule == "yn_do") {
            parts.boolean = true;
            parts.subject.push_back(build(child.at(1)));
            parts.verb = build(child.at(2));
        } else {
            throw Rejected("no semantic rule named " + rule);
        }
        return assemble(std::move(parts));
    }
};

}  // namespace

Interpretation Interpreter::interpret(const Tree& tree, const std::vector<Token>& tokens) const {
    Interpretation result;
    Composer composer(*grammar_, *morph_, *schema_, *kb_, tokens);
    try {
        result.query = composer.run(tree);
        result.attachment_cost = composer.cost();
        result.ok = true;
    } catch (const Rejected& rejected) {
        result.reason = rejected.what();
        result.ok = false;
    }
    return result;
}

}  // namespace lex
