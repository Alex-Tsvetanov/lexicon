#include <string>

#include "check.hpp"
#include "lexicon/pipeline.hpp"

namespace {

const lex::System& system() {
    static lex::System loaded = lex::System::load();
    return loaded;
}

}  // namespace

LEX_TEST(pipeline_end_to_end_answers) {
    const lex::Analysis analysis = system().analyse("Which films did Nolan direct in 2010?");
    CHECK(analysis.parsed);
    CHECK(analysis.answered());

    // Every stage produced something, which is what the demo prints.
    CHECK_EQ(analysis.tokens.size(), std::size_t{7});
    CHECK(!analysis.tokens.front().readings.empty());
    CHECK(analysis.forest_nodes > 0);
    CHECK(analysis.parse_microseconds > 0.0);
    CHECK_CONTAINS(analysis.sql, "SELECT DISTINCT");
    CHECK_CONTAINS(analysis.sparql, "SELECT DISTINCT ?x0");
    CHECK_CONTAINS(analysis.paraphrase, "You are asking for the films");
    CHECK_EQ(analysis.answer.str(), std::string("Inception"));

    // The tokeniser drops punctuation and keeps every reading of a word form.
    CHECK_EQ(analysis.tokens.at(1).text, std::string("films"));
    CHECK(analysis.tokens.at(1).readings.size() >= 2);
}

LEX_TEST(pipeline_reports_ambiguity) {
    const lex::Analysis simple = system().analyse("Who directed Inception?");
    CHECK_EQ(simple.derivations, std::uint64_t{1});
    CHECK_EQ(simple.ambiguity_points, std::size_t{0});

    const lex::Analysis ambiguous =
        system().analyse("Which films with actors from France did Nolan direct?");
    CHECK(ambiguous.derivations > 1);
    CHECK(ambiguous.ambiguity_points > 0);
    CHECK(ambiguous.distinct_forms > 1);
    CHECK_CONTAINS(ambiguous.choice_reason, "2 distinct logical form");

    // A sentence outside the grammar fails as a parse failure, and a sentence
    // outside the lexicon fails in the semantic layer. The two are reported
    // differently on purpose.
    const lex::Analysis ungrammatical = system().analyse("Which films did Nolan direct or produce?");
    CHECK(!ungrammatical.parsed);
    CHECK_CONTAINS(ungrammatical.failure, "no parse");

    const lex::Analysis unknown_word = system().analyse("Which films did Kubrick direct?");
    CHECK(unknown_word.parsed);
    CHECK(!unknown_word.answered());
    CHECK_CONTAINS(unknown_word.failure, "knowledge model");
}

LEX_TEST(pipeline_coverage_over_question_set) {
    const std::vector<std::string> questions = lex::System::question_set();
    CHECK(questions.size() >= 20);

    std::size_t answered = 0;
    std::size_t parsed = 0;
    for (const std::string& question : questions) {
        const lex::Analysis analysis = system().analyse(question);
        parsed += analysis.parsed ? 1 : 0;
        answered += analysis.answered() ? 1 : 0;
        // Whatever happens, the system says which stage gave up.
        if (!analysis.answered()) CHECK(!analysis.failure.empty());
    }

    // The three questions at the end of the set are known to be out of scope,
    // and they are in the set so that the coverage figure has something to fail
    // on. If this number moves, the question set or the grammar moved with it.
    CHECK_EQ(answered, questions.size() - 3);
    CHECK_EQ(parsed, questions.size() - 2);
}
