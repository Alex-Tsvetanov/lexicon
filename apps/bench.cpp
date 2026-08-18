// The measurements reported in the documentation.
//
// Three experiments: parse time against sentence length, ambiguity against the
// size of the grammar, and coverage over the bundled question set with the
// failures listed rather than summarised away.
//
// Everything is timed with std::chrono::steady_clock and repeated, because a
// single run of a sub millisecond parse measures the clock, not the parser.
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "lexicon/pipeline.hpp"

namespace {

struct Timing {
    double median = 0.0;
    double best = 0.0;
};

Timing time_parse(const lex::Grammar& grammar, const std::vector<lex::Token>& tokens,
                  int repetitions) {
    const lex::Parser parser(grammar);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repetitions));
    for (int i = 0; i < repetitions; ++i) {
        const auto started = std::chrono::steady_clock::now();
        const lex::ParseResult result = parser.parse(tokens);
        const auto finished = std::chrono::steady_clock::now();
        // Read something off the result so the call cannot be optimised away.
        if (result.node_count() == 0 && result.accepted()) std::cout << "";
        samples.push_back(std::chrono::duration<double, std::micro>(finished - started).count());
    }
    std::sort(samples.begin(), samples.end());
    Timing timing;
    timing.best = samples.front();
    timing.median = samples[samples.size() / 2];
    return timing;
}

// A family of sentences of growing length: each step adds one prepositional
// phrase, which adds three words and one more attachment site.
std::string sentence_with(int phrases) {
    std::string question = "Who directed a film";
    const char* additions[] = {" with Cotillard", " in 2010", " in France", " with Hardy",
                               " with Murphy", " with DiCaprio", " with Adams", " with Tautou"};
    for (int i = 0; i < phrases; ++i)
        question += additions[static_cast<std::size_t>(i) % (sizeof(additions) / sizeof(*additions))];
    return question + "?";
}

void parse_time_against_length(const lex::System& system) {
    std::cout << "\n== Parse time against sentence length ==\n";
    std::cout << "phrases  words  derivations  chart items  forest nodes  median us  best us\n";
    for (int phrases = 0; phrases <= 6; ++phrases) {
        const std::string question = sentence_with(phrases);
        const std::vector<lex::Token> tokens = lex::tokenize(system.morphology(), question);
        const lex::ParseResult result = lex::Parser(system.grammar()).parse(tokens);
        const Timing timing = time_parse(system.grammar(), tokens, 201);
        std::cout << std::setw(7) << phrases << "  " << std::setw(5) << tokens.size() << "  "
                  << std::setw(11) << result.derivation_count() << "  " << std::setw(11)
                  << result.item_count() << "  " << std::setw(12) << result.node_count() << "  "
                  << std::setw(9) << std::fixed << std::setprecision(1) << timing.median << "  "
                  << std::setw(7) << timing.best << "\n";
    }
}

void ambiguity_against_grammar_size() {
    std::cout << "\n== Ambiguity against grammar size ==\n";
    std::cout << "Three grammars over the same question set. The second and the third add\n"
                 "productions that describe nothing the first cannot already describe.\n\n";
    std::cout << "grammar        rules  parsed  derivations  distinct forms  ambiguity points\n";
    for (const char* file : {"grammar_en.txt", "grammar_v2.txt", "grammar_v3.txt"}) {
        const lex::System system = lex::System::load(file);
        std::uint64_t derivations = 0;
        std::size_t points = 0;
        std::size_t parsed = 0;
        std::size_t distinct = 0;
        for (const std::string& question : lex::System::question_set()) {
            const lex::Analysis analysis = system.analyse(question);
            if (!analysis.parsed) continue;
            ++parsed;
            derivations += analysis.derivations;
            points += analysis.ambiguity_points;
            distinct += analysis.distinct_forms;
        }
        std::cout << std::setw(14) << std::left << file << std::right << std::setw(5)
                  << system.grammar().rules().size() << "  " << std::setw(6) << parsed << "  "
                  << std::setw(11) << derivations << "  " << std::setw(14) << distinct << "  "
                  << std::setw(16) << points << "\n";
    }
}

void coverage(const lex::System& system) {
    std::cout << "\n== Coverage over the bundled question set ==\n";
    const std::vector<std::string> questions = lex::System::question_set();
    std::vector<std::string> failures;
    std::size_t parsed = 0;
    std::size_t answered = 0;
    std::size_t ambiguous = 0;
    double total_parse = 0.0;

    for (const std::string& question : questions) {
        const lex::Analysis analysis = system.analyse(question);
        parsed += analysis.parsed ? 1 : 0;
        answered += analysis.answered() ? 1 : 0;
        ambiguous += analysis.derivations > 1 ? 1 : 0;
        total_parse += analysis.parse_microseconds;
        if (!analysis.answered()) failures.push_back(question + "  ->  " + analysis.failure);
    }

    std::cout << "questions               " << questions.size() << "\n";
    std::cout << "parsed                  " << parsed << "\n";
    std::cout << "answered                " << answered << "\n";
    std::cout << "more than one parse     " << ambiguous << "\n";
    std::cout << "mean parse time         " << std::fixed << std::setprecision(1)
              << total_parse / static_cast<double>(questions.size()) << " us\n";
    std::cout << "\nfailures, in full:\n";
    for (const std::string& failure : failures) std::cout << "  " << failure << "\n";
    if (failures.empty()) std::cout << "  (none)\n";
}

void resources(const lex::System& system) {
    std::cout << "\n== Size of the compiled resources ==\n";
    std::cout << "lexemes                 " << system.morphology().lexeme_count() << "\n";
    std::cout << "surface forms           " << system.morphology().form_count() << "\n";
    std::cout << "transducer states       " << system.morphology().state_count() << "\n";
    std::cout << "transducer transitions  " << system.morphology().edge_count() << "\n";
    std::cout << "grammar productions     " << system.grammar().rules().size() << "\n";
    std::cout << "grammar symbols         " << system.grammar().symbol_count() << "\n";
    std::cout << "relations in the model  " << system.schema().relations().size() << "\n";
    std::cout << "prepositional frames    " << system.schema().frames().size() << "\n";
    std::cout << "entities                " << system.knowledge().entity_count() << "\n";
    std::cout << "facts                   " << system.knowledge().fact_count() << "\n";
}

}  // namespace

int main() {
    try {
        const lex::System system = lex::System::load();
        std::cout << "Lexicon benchmark. Every number below was produced by this run.\n";
        resources(system);
        parse_time_against_length(system);
        ambiguity_against_grammar_size();
        coverage(system);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "lexicon_bench: " << error.what() << "\n";
        return 1;
    }
}
