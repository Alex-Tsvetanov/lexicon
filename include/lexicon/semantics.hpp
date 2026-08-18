// The semantic layer: from a parse tree to a logical form.
//
// The mapping is compositional. Every production of the grammar names a semantic
// rule, and that rule builds the meaning of the phrase from the meanings of its
// parts. Nothing here looks at the surface string.
//
// The layer is also where readings die. Every atom it builds is type checked
// against the knowledge model, and a preposition only becomes a relation if the
// model has a frame for that preposition with those two types. A reading that
// cannot be mapped onto anything in the model is rejected with a reason, and
// that rejection, rather than an early heuristic, is how the system resolves
// ambiguity.
#pragma once
#include <string>
#include <vector>

#include "lexicon/earley.hpp"
#include "lexicon/kb.hpp"
#include "lexicon/logic.hpp"

namespace lex {

struct TypeSpec {
    std::string name;
    bool scalar = false;
    std::string table;
    std::string key;
    std::string label;
    std::string iri;
};

struct RelationSpec {
    std::string name;
    std::vector<std::string> args;
    std::string store;  // "link" for a table of pairs, "attr" for a column
    std::string table;
    std::vector<std::string> cols;
    std::string iri;
};

struct PrepFrame {
    std::string prep;
    std::string arg_type;
    std::string attach_type;
    std::string relation;
    bool arg_first = false;  // relation(arg, attach) instead of relation(attach, arg)
};

class Schema {
public:
    static Schema load(const std::string& path);

    const TypeSpec* type(const std::string& name) const;
    const RelationSpec* relation(const std::string& name) const;
    const PrepFrame* frame(const std::string& prep, const std::string& arg_type,
                           const std::string& attach_type) const;

    const std::vector<TypeSpec>& types() const { return types_; }
    const std::vector<RelationSpec>& relations() const { return relations_; }
    const std::vector<PrepFrame>& frames() const { return frames_; }

private:
    std::vector<TypeSpec> types_;
    std::vector<RelationSpec> relations_;
    std::vector<PrepFrame> frames_;
};

struct Interpretation {
    bool ok = false;
    Query query;
    std::string reason;       // why the reading was rejected, empty when it was not
    int attachment_cost = 0;  // sum of the distances of the prepositional attachments
};

class Interpreter {
public:
    Interpreter(const Grammar& grammar, const Morphology& morph, const Schema& schema,
                const KnowledgeBase& kb)
        : grammar_(&grammar), morph_(&morph), schema_(&schema), kb_(&kb) {}

    Interpretation interpret(const Tree& tree, const std::vector<Token>& tokens) const;

private:
    const Grammar* grammar_;
    const Morphology* morph_;
    const Schema* schema_;
    const KnowledgeBase* kb_;
};

}  // namespace lex
