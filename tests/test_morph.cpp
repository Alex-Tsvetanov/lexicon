#include <algorithm>
#include <string>

#include "check.hpp"
#include "lexicon/morph.hpp"

namespace {

const lex::Morphology& english() {
    static lex::Morphology morph = lex::Morphology::load(lex::data_path("lexicon_en.txt"));
    return morph;
}

const lex::Morphology& bulgarian() {
    static lex::Morphology morph = lex::Morphology::load(lex::data_path("lexicon_bg.txt"));
    return morph;
}

bool has_reading(const std::vector<lex::Reading>& readings, const std::string& lemma,
                 const std::string& pos, const std::string& feats) {
    return std::any_of(readings.begin(), readings.end(), [&](const lex::Reading& reading) {
        return reading.lemma == lemma && reading.pos == pos &&
               reading.feats.satisfies(lex::Features::parse(feats));
    });
}

}  // namespace

LEX_TEST(morph_noun_singular_and_plural) {
    const auto singular = english().analyze("film");
    CHECK(has_reading(singular, "film", "noun", "num=sg"));

    const auto plural = english().analyze("films");
    CHECK(has_reading(plural, "film", "noun", "num=pl"));
    CHECK(!has_reading(plural, "film", "noun", "num=sg"));

    CHECK(english().analyze("filmz").empty());
}

LEX_TEST(morph_orthographic_rules) {
    // The deletion in "~1+ies" is data, not a hard coded English rule.
    CHECK(has_reading(english().analyze("countries"), "country", "noun", "num=pl"));
    CHECK(english().analyze("countrys").empty());
    CHECK(has_reading(english().analyze("actresses"), "actress", "noun", "num=pl"));
    CHECK(has_reading(english().analyze("produced"), "produce", "verb", "vform=past"));
    CHECK(english().analyze("produceed").empty());
}

LEX_TEST(morph_verb_paradigm) {
    const auto past = english().analyze("directed");
    // "directed" is genuinely two readings: the past tense and the participle.
    CHECK(has_reading(past, "direct", "verb", "vform=past"));
    CHECK(has_reading(past, "direct", "verb", "vform=part"));
    CHECK_EQ(past.size(), std::size_t{2});

    CHECK(has_reading(english().analyze("directs"), "direct", "verb", "vform=fin pers=3"));
    CHECK(has_reading(english().analyze("direct"), "direct", "verb", "vform=base"));
}

LEX_TEST(morph_homonymy_films) {
    // "films" is both the plural noun and the third person verb form. Both
    // readings must survive, because only the parser can tell them apart.
    const auto readings = english().analyze("films");
    CHECK(has_reading(readings, "film", "noun", "num=pl"));
    CHECK(has_reading(readings, "film", "verb", "vform=fin"));
    CHECK(readings.size() >= 2);
}

LEX_TEST(morph_generation_roundtrip) {
    // The transducer runs backwards for the sentence synthesiser.
    const auto plural = english().generate("film", "noun", lex::Features::parse("num=pl"));
    CHECK(plural.has_value());
    CHECK_EQ(*plural, std::string("films"));

    const auto participle = english().generate("direct", "verb", lex::Features::parse("vform=part"));
    CHECK(participle.has_value());
    CHECK_EQ(*participle, std::string("directed"));

    const auto countries = english().generate("country", "noun", lex::Features::parse("num=pl"));
    CHECK(countries.has_value());
    CHECK_EQ(*countries, std::string("countries"));

    CHECK(!english().generate("film", "noun", lex::Features::parse("num=dual")).has_value());

    // Every generated form is analysable again, which is what "one resource in
    // both directions" has to mean in practice.
    CHECK(has_reading(english().analyze(*plural), "film", "noun", "num=pl"));
}

LEX_TEST(morph_unknown_word_guess) {
    CHECK(english().analyze("sculptors").empty());
    const auto guesses = english().guess("sculptors");
    CHECK(!guesses.empty());
    CHECK(has_reading(guesses, "sculptor", "noun", "num=pl"));
    for (const auto& reading : guesses) CHECK(reading.guessed);
}

LEX_TEST(morph_bulgarian_definite_article) {
    // Малкият български лексикон: членуване и число от същия механизъм.
    CHECK(has_reading(bulgarian().analyze("филм"), "филм", "noun", "num=sg"));
    CHECK(has_reading(bulgarian().analyze("филмът"), "филм", "noun", "num=sg def=yes"));
    CHECK(has_reading(bulgarian().analyze("филмите"), "филм", "noun", "num=pl def=yes"));
    CHECK(has_reading(bulgarian().analyze("режисьори"), "режисьор", "noun", "num=pl"));

    // A character wise deletion, not a byte wise one: "държава" loses one letter.
    CHECK(has_reading(bulgarian().analyze("държави"), "държава", "noun", "num=pl"));
    CHECK(has_reading(bulgarian().analyze("държавата"), "държава", "noun", "num=sg def=yes"));

    const auto definite = bulgarian().generate("актьор", "noun", lex::Features::parse("num=pl def=yes"));
    CHECK(definite.has_value());
    CHECK_EQ(*definite, std::string("актьорите"));
}
