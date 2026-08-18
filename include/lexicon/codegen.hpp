// Two code generators over the same logical form.
//
// Neither of them looks at the question, at the parse tree or at the lexicon.
// They read the logical form and the schema, and nothing else. That is the test
// of whether the logical form is an abstraction or a disguised template: if it
// were a template, a second target language would not come out of it.
#pragma once
#include <string>

#include "lexicon/logic.hpp"
#include "lexicon/semantics.hpp"

namespace lex {

// A SQL query against the relational rendering of the knowledge model.
std::string to_sql(const Schema& schema, const Query& query);

// A SPARQL query against the graph rendering of the same model.
std::string to_sparql(const Schema& schema, const Query& query);

// The relational schema and the data, so the generated SQL can be run against a
// real database, and the same facts as Turtle for the SPARQL side.
std::string sql_schema_and_data(const Schema& schema, const KnowledgeBase& kb);
std::string turtle_data(const Schema& schema, const KnowledgeBase& kb);

}  // namespace lex
