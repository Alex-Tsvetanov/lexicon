#include <string>

#include "check.hpp"
#include "lexicon/pipeline.hpp"

namespace {

const lex::System& system() {
    static lex::System loaded = lex::System::load();
    return loaded;
}

lex::Query query_for(const std::string& question) {
    const lex::Analysis analysis = system().analyse(question);
    return analysis.readings.at(static_cast<std::size_t>(analysis.chosen)).query;
}

}  // namespace

LEX_TEST(codegen_sql_projection_and_join) {
    const std::string sql = lex::to_sql(system().schema(), query_for("Which films did Nolan direct?"));
    CHECK_CONTAINS(sql, "SELECT DISTINCT");
    CHECK_CONTAINS(sql, ".title AS answer");
    CHECK_CONTAINS(sql, "film AS t0");
    CHECK_CONTAINS(sql, "directed AS r");
    CHECK_CONTAINS(sql, "person AS e");
    CHECK_CONTAINS(sql, "ORDER BY 1;");
    // The entity is a condition on a key, never a string pasted into the query.
    CHECK_CONTAINS(sql, ".id = 'p_nolan'");

    // An attribute relation becomes a column of the table it belongs to, not a
    // join, which is the whole reason the schema distinguishes the two.
    const std::string with_year =
        lex::to_sql(system().schema(), query_for("Which films did Nolan direct in 2010?"));
    CHECK_CONTAINS(with_year, ".year = 2010");

    const std::string counted =
        lex::to_sql(system().schema(), query_for("How many films did Nolan direct?"));
    CHECK_CONTAINS(counted, "SELECT COUNT(DISTINCT");
}

LEX_TEST(codegen_sql_negation) {
    const std::string sql =
        lex::to_sql(system().schema(), query_for("Which films were not directed by Nolan?"));
    CHECK_CONTAINS(sql, "NOT EXISTS (SELECT 1");
    CHECK_CONTAINS(sql, "film AS t0");

    // The universal becomes relational division: no witness of the restriction
    // fails the body.
    const std::string division =
        lex::to_sql(system().schema(), query_for("Which actors acted in every film by Villeneuve?"));
    const std::size_t first = division.find("NOT EXISTS");
    CHECK(first != std::string::npos);
    CHECK(division.find("NOT EXISTS", first + 1) != std::string::npos);

    const std::string boolean =
        lex::to_sql(system().schema(), query_for("Did Nolan direct Inception?"));
    CHECK_CONTAINS(boolean, "SELECT EXISTS (");
}

LEX_TEST(codegen_sparql_triples) {
    const std::string sparql =
        lex::to_sparql(system().schema(), query_for("Which films did Nolan direct in 2010?"));
    CHECK_CONTAINS(sparql, "PREFIX : <http://example.org/film#>");
    CHECK_CONTAINS(sparql, "SELECT DISTINCT ?x0");
    CHECK_CONTAINS(sparql, "?x0 a :Film .");
    CHECK_CONTAINS(sparql, ":p_nolan :directed ?x0 .");
    CHECK_CONTAINS(sparql, "?x0 :releasedIn 2010 .");

    const std::string negated =
        lex::to_sparql(system().schema(), query_for("Which films were not directed by Nolan?"));
    CHECK_CONTAINS(negated, "FILTER NOT EXISTS {");

    const std::string counted =
        lex::to_sparql(system().schema(), query_for("How many films did Nolan direct?"));
    CHECK_CONTAINS(counted, "SELECT (COUNT(DISTINCT ?x0) AS ?answer)");

    CHECK_CONTAINS(lex::to_sparql(system().schema(), query_for("Did Nolan direct Inception?")),
                   "ASK WHERE");
}

LEX_TEST(codegen_both_targets_same_form) {
    // The same logical form drives both generators. If either of them were a
    // template over the question, this would not hold.
    for (const char* question : {"Which films did Nolan direct?",
                                 "Which films were directed by Nolan?"}) {
        const lex::Query query = query_for(question);
        CHECK(!lex::to_sql(system().schema(), query).empty());
        CHECK(!lex::to_sparql(system().schema(), query).empty());
    }
    const lex::Query active = query_for("Which films did Nolan direct?");
    const lex::Query passive = query_for("Which films were directed by Nolan?");
    CHECK_EQ(lex::to_sql(system().schema(), active), lex::to_sql(system().schema(), passive));
    CHECK_EQ(lex::to_sparql(system().schema(), active), lex::to_sparql(system().schema(), passive));
}

LEX_TEST(synth_paraphrase_mentions_relations) {
    const lex::Analysis analysis =
        system().analyse("Which films with actors from France did Nolan direct?");
    CHECK_CONTAINS(analysis.paraphrase, "films");
    CHECK_CONTAINS(analysis.paraphrase, "with actors from France");
    CHECK_CONTAINS(analysis.paraphrase, "directed by Christopher Nolan");

    // The paraphrase is built from the logical form, so the two readings of the
    // same sentence have to come out as two different sentences. Otherwise the
    // confirmation the user gives would mean nothing.
    const lex::Query other = analysis.readings.at(0).ok && analysis.chosen != 0
                                 ? analysis.readings.at(0).query
                                 : analysis.readings.at(1).query;
    const std::string alternative =
        lex::paraphrase(system().schema(), system().morphology(), system().knowledge(), other);
    CHECK(alternative != analysis.paraphrase);

    // Inflection comes from the transducer running backwards.
    CHECK_CONTAINS(system().analyse("Who directed Inception?").paraphrase, "people");
    CHECK_CONTAINS(system().analyse("How many films did Nolan direct?").paraphrase,
                   "the number of films");
    CHECK_CONTAINS(system().analyse("Did Nolan direct Inception?").paraphrase,
                   "whether Christopher Nolan directed Inception");
    CHECK_CONTAINS(system().analyse("Which actors acted in every film by Villeneuve?").paraphrase,
                   "every film by Denis Villeneuve");
}

LEX_TEST(kb_evaluates_conjunction) {
    const lex::Analysis analysis = system().analyse("Which actors acted in Inception?");
    CHECK_EQ(analysis.answer.rows.size(), std::size_t{4});
    CHECK_EQ(analysis.answer.rows.front(), std::string("Cillian Murphy"));
    CHECK_CONTAINS(analysis.answer.str(), "Leonardo DiCaprio");

    const lex::Analysis joined =
        system().analyse("Which actors from France acted in films by Nolan?");
    CHECK_EQ(joined.answer.rows.size(), std::size_t{1});
    CHECK_EQ(joined.answer.rows.front(), std::string("Marion Cotillard"));
}

LEX_TEST(kb_evaluates_negation_and_count) {
    const lex::Analysis negated = system().analyse("Which films were not directed by Nolan?");
    CHECK_EQ(negated.answer.str(), std::string("Amelie, Arrival"));

    const lex::Analysis counted = system().analyse("How many actors acted in Dunkirk?");
    CHECK(counted.answer.is_count);
    CHECK_EQ(counted.answer.count, 2LL);

    const lex::Analysis empty = system().analyse("Which films did Warner produce?");
    CHECK_EQ(empty.answer.str(), std::string("Dunkirk, Inception"));
}

LEX_TEST(kb_evaluates_universal) {
    const lex::Analysis analysis =
        system().analyse("Which actors acted in every film by Villeneuve?");
    CHECK_EQ(analysis.answer.str(), std::string("Amy Adams"));

    // Nobody is in all three of the films of the other director, and the
    // evaluator has to say so rather than returning the ones who are in some.
    const lex::Analysis harder = system().analyse("Which actors acted in every film by Nolan?");
    CHECK(harder.answered());
    CHECK(harder.answer.rows.empty());
}
