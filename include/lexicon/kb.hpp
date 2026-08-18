// The bundled knowledge base, and an evaluator for the logical form.
//
// The knowledge base is a set of typed entities and a set of ground facts. The
// evaluator answers a Query directly over those facts, which is what lets the
// demo print an answer next to the generated queries. The generated SQL and
// SPARQL describe the same question against the relational and the graph
// rendering of the same facts; the evaluator is the reference that the SQL is
// checked against by tools/check_sql.py.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "lexicon/logic.hpp"

namespace lex {

struct Value {
    bool numeric = false;
    long long number = 0;
    std::string id;

    std::string str() const;
    bool operator==(const Value& other) const;
    bool operator<(const Value& other) const;
};

class KnowledgeBase {
public:
    static KnowledgeBase load(const std::string& path);

    const std::string* type_of(const std::string& entity) const;
    const std::string* label_of(const std::string& entity) const;
    const std::vector<std::string>& entities_of_type(const std::string& type) const;
    const std::vector<std::vector<Value>>* extension(const std::string& relation) const;

    const std::map<std::string, std::vector<std::string>>& types() const { return by_type_; }
    const std::map<std::string, std::vector<std::vector<Value>>>& relations() const {
        return relations_;
    }
    std::size_t entity_count() const { return entity_types_.size(); }
    std::size_t fact_count() const;

private:
    std::map<std::string, std::string> entity_types_;
    std::map<std::string, std::string> entity_labels_;
    std::map<std::string, std::vector<std::string>> by_type_;
    std::map<std::string, std::vector<std::vector<Value>>> relations_;
};

struct Answer {
    bool is_boolean = false;
    bool is_count = false;
    bool boolean_value = false;
    long long count = 0;
    std::vector<std::string> rows;  // labels, sorted, without repetition

    std::string str() const;
};

Answer evaluate(const KnowledgeBase& kb, const Query& query);

}  // namespace lex
