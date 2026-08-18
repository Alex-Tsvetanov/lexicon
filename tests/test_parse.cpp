#include <set>
#include <string>

#include "check.hpp"
#include "lexicon/earley.hpp"

namespace {

const lex::Morphology& morph() {
    static lex::Morphology loaded = lex::Morphology::load(lex::data_path("lexicon_en.txt"));
    return loaded;
}

const lex::Grammar& grammar() {
    static lex::Grammar loaded = lex::Grammar::load(lex::data_path("grammar_en.txt"));
    return loaded;
}

lex::ParseResult parse(const std::string& question) {
    return lex::Parser(grammar()).parse(lex::tokenize(morph(), question));
}

std::size_t tree_size(const lex::Tree& tree) {
    std::size_t total = 1;
    for (const lex::Tree& child : tree.children) total += tree_size(child);
    return total;
}

std::string serialise(const lex::Tree& tree) {
    if (tree.children.empty()) return grammar().name(tree.symbol);
    std::string out = "(" + grammar().name(tree.symbol);
    for (const lex::Tree& child : tree.children) out += " " + serialise(child);
    return out + ")";
}

}  // namespace

LEX_TEST(grammar_loads_and_indexes) {
    CHECK(grammar().rules().size() >= 20);
    CHECK_EQ(grammar().name(grammar().start()), std::string("S"));
    CHECK(grammar().is_terminal(grammar().symbol("V")));
    CHECK(!grammar().is_terminal(grammar().symbol("VP")));
    CHECK(!grammar().rules_for(grammar().symbol("VP")).empty());

    // A terminal is a part of speech plus a feature constraint, so the two
    // readings of "directed" land on two different terminals.
    const auto readings = morph().analyze("directed");
    CHECK_EQ(readings.size(), std::size_t{2});
    int as_finite = 0;
    int as_participle = 0;
    for (const auto& reading : readings) {
        as_finite += grammar().accepts(grammar().symbol("V"), reading) ? 1 : 0;
        as_participle += grammar().accepts(grammar().symbol("VPART"), reading) ? 1 : 0;
    }
    CHECK_EQ(as_finite, 1);
    CHECK_EQ(as_participle, 1);
}

LEX_TEST(earley_accepts_simple_question) {
    const auto result = parse("Who directed Inception?");
    CHECK(result.accepted());
    CHECK_EQ(result.derivation_count(), std::uint64_t{1});

    for (const char* question : {"Which films were directed by Nolan?",
                                 "How many films did Nolan direct?",
                                 "Did Nolan direct Inception?",
                                 "Which films did Nolan not direct?",
                                 "Which films were not directed by Nolan?"}) {
        const auto parsed = parse(question);
        CHECK(parsed.accepted());
        CHECK_EQ(parsed.derivation_count(), std::uint64_t{1});
    }
}

LEX_TEST(earley_rejects_ungrammatical) {
    CHECK(!parse("Nolan films purple").accepted());
    CHECK(!parse("did did did").accepted());
    CHECK(!parse("Which films Nolan").accepted());
    CHECK(!parse("").accepted());
}

LEX_TEST(earley_counts_attachment_ambiguity) {
    // Every prepositional phrase after the verb can close the noun phrase or
    // attach higher, so k phrases give k+1 readings.
    CHECK_EQ(parse("Who directed a film with Cotillard?").derivation_count(),
             std::uint64_t{2});
    CHECK_EQ(parse("Who directed a film with Cotillard in 2010?").derivation_count(),
             std::uint64_t{3});
    CHECK_EQ(parse("Who directed a film with Cotillard in 2010 in France?").derivation_count(),
             std::uint64_t{4});

    // Ambiguity inside the questioned noun phrase, which is the case where both
    // readings survive the semantic layer.
    const auto ambiguous = parse("Which films with actors from France did Nolan direct?");
    CHECK_EQ(ambiguous.derivation_count(), std::uint64_t{2});
    CHECK_EQ(ambiguous.ambiguity_points(), std::size_t{1});
}

LEX_TEST(earley_forest_is_shared) {
    const auto result = parse("Who directed a film with Cotillard in 2010 in France?");
    CHECK_EQ(result.derivation_count(), std::uint64_t{4});

    std::size_t separate = 0;
    for (const lex::Tree& tree : result.enumerate(100)) separate += tree_size(tree);

    // The forest holds every reading in fewer nodes than the readings need when
    // they are written out one by one. That difference is the packing.
    CHECK(result.node_count() < separate);
    CHECK(result.ambiguity_points() < result.node_count());
}

LEX_TEST(earley_enumerates_distinct_trees) {
    const auto result = parse("Who directed a film with Cotillard in 2010?");
    const auto trees = result.enumerate(100);
    CHECK_EQ(trees.size(), std::size_t{3});

    std::set<std::string> shapes;
    for (const lex::Tree& tree : trees) shapes.insert(serialise(tree));
    CHECK_EQ(shapes.size(), std::size_t{3});

    // The limit is honoured, which is what keeps a badly ambiguous sentence from
    // turning into an unbounded amount of work.
    CHECK_EQ(result.enumerate(1).size(), std::size_t{1});
    CHECK(result.enumerate(0).empty());
}
