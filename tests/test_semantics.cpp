#include <set>
#include <string>

#include "check.hpp"
#include "lexicon/pipeline.hpp"

namespace {

const lex::System& system() {
    static lex::System loaded = lex::System::load();
    return loaded;
}

std::size_t rejected_count(const lex::Analysis& analysis) {
    std::size_t total = 0;
    for (const lex::ReadingRecord& record : analysis.readings) total += record.ok ? 0 : 1;
    return total;
}

const lex::ReadingRecord& chosen(const lex::Analysis& analysis) {
    return analysis.readings.at(static_cast<std::size_t>(analysis.chosen));
}

}  // namespace

LEX_TEST(semantics_type_check_prunes_reading) {
    // Two attachment sites, one of which the knowledge model has no relation
    // for. The parser offers both; the semantic layer is what says no.
    const lex::Analysis analysis =
        system().analyse("Which films with actors in 2010 did Nolan direct?");
    CHECK_EQ(analysis.derivations, std::uint64_t{2});
    CHECK_EQ(analysis.readings.size(), std::size_t{2});
    CHECK_EQ(rejected_count(analysis), std::size_t{1});
    CHECK_EQ(analysis.accepted_readings, std::size_t{1});
    CHECK(analysis.answered());
    CHECK_CONTAINS(chosen(analysis).logical_form, "released(x0, 2010)");

    for (const lex::ReadingRecord& record : analysis.readings)
        if (!record.ok) CHECK_CONTAINS(record.reason, "between Person and Year");

    // When no reading survives the type check the system reports that, rather
    // than returning the least bad query.
    const lex::Analysis hopeless = system().analyse("Which films did Nolan direct in France?");
    CHECK(hopeless.parsed);
    CHECK(!hopeless.answered());
    CHECK_CONTAINS(hopeless.failure, "maps onto the knowledge model");
}

LEX_TEST(semantics_ambiguous_question_two_forms) {
    // Genuine ambiguity: the country can belong to the actors or to the films,
    // and the model has a relation for both, so both readings survive.
    const lex::Analysis analysis =
        system().analyse("Which films with actors from France did Nolan direct?");
    CHECK_EQ(analysis.derivations, std::uint64_t{2});
    CHECK_EQ(analysis.accepted_readings, std::size_t{2});
    CHECK_EQ(analysis.distinct_forms, std::size_t{2});

    std::set<std::string> forms;
    for (const lex::ReadingRecord& record : analysis.readings)
        if (record.ok) forms.insert(record.canonical);
    CHECK_EQ(forms.size(), std::size_t{2});

    // Late closure: the reading that attaches to the nearer noun is chosen, and
    // that is the one about the actors.
    CHECK_CONTAINS(chosen(analysis).logical_form, "person_country(x1, c_france)");
    CHECK_EQ(chosen(analysis).attachment_cost, 2);
    CHECK_CONTAINS(analysis.choice_reason, "closest attachment");
}

LEX_TEST(semantics_negation) {
    const lex::Analysis active = system().analyse("Which films did Nolan not direct?");
    CHECK(active.answered());
    CHECK_CONTAINS(chosen(active).logical_form, "not directed(p_nolan, x0)");
    CHECK_CONTAINS(chosen(active).logical_form, "Film(x0)");

    // The passive says the same thing, so the logical forms have to match.
    const lex::Analysis passive = system().analyse("Which films were not directed by Nolan?");
    CHECK(passive.answered());
    CHECK_EQ(chosen(passive).canonical, chosen(active).canonical);

    // The negation is inside, the restriction outside: without that the answer
    // would be every film in the base.
    CHECK_EQ(active.answer.rows.size(), std::size_t{2});
}

LEX_TEST(semantics_count_query) {
    const lex::Analysis analysis = system().analyse("How many films did Nolan direct?");
    CHECK(analysis.answered());
    CHECK(chosen(analysis).query.count);
    CHECK(!chosen(analysis).query.boolean);
    CHECK_CONTAINS(chosen(analysis).logical_form, "count x0");
    CHECK_EQ(analysis.answer.count, 3LL);

    const lex::Analysis yes_no = system().analyse("Did Nolan direct Inception?");
    CHECK(yes_no.answered());
    CHECK(chosen(yes_no).query.boolean);
    CHECK(yes_no.answer.boolean_value);
    CHECK(!system().analyse("Did Villeneuve direct Inception?").answer.boolean_value);
}

LEX_TEST(semantics_universal_quantifier) {
    const lex::Analysis analysis =
        system().analyse("Which actors acted in every film by Villeneuve?");
    CHECK(analysis.answered());
    const std::string form = chosen(analysis).logical_form;
    CHECK_CONTAINS(form, "forall");
    // The restriction of the quantifier carries the relative clause, and the
    // body carries what the sentence asserts about it.
    CHECK_CONTAINS(form, "directed(p_villeneuve, x1)");
    CHECK_CONTAINS(form, "acted_in(x0, x1)");
    CHECK_EQ(analysis.answer.rows.size(), std::size_t{1});
    CHECK_EQ(analysis.answer.rows.front(), std::string("Amy Adams"));

    // "no" is the other quantifier, and it is not the same as "not".
    const lex::Analysis none = system().analyse("Which actors acted in no film by Nolan?");
    CHECK(none.answered());
    CHECK_CONTAINS(chosen(none).logical_form, "not ");
}

LEX_TEST(semantics_duplicate_forms_collapse) {
    // Three parses, one meaning: attaching a prepositional phrase to the verb
    // phrase or to the object noun restricts the same referent either way.
    const lex::Analysis analysis = system().analyse("Who directed a film with Cotillard in 2010?");
    CHECK_EQ(analysis.derivations, std::uint64_t{3});
    CHECK_EQ(analysis.accepted_readings, std::size_t{3});
    CHECK_EQ(analysis.distinct_forms, std::size_t{1});

    std::size_t duplicates = 0;
    for (const lex::ReadingRecord& record : analysis.readings)
        duplicates += record.duplicate_of_earlier ? 1 : 0;
    CHECK_EQ(duplicates, std::size_t{2});
    CHECK_CONTAINS(analysis.choice_reason, "differ only in the tree");
}
