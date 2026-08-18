#include "lexicon/pipeline.hpp"

#include <chrono>
#include <map>

namespace lex {

System System::load() { return load("grammar_en.txt"); }

System System::load(const std::string& grammar_file) {
    System system;
    system.morph_ = Morphology::load(data_path("lexicon_en.txt"));
    system.grammar_ = Grammar::load(data_path(grammar_file));
    system.schema_ = Schema::load(data_path("schema.txt"));
    system.kb_ = KnowledgeBase::load(data_path("kb.txt"));
    return system;
}

std::vector<std::string> System::question_set() {
    return read_config_lines(data_path("questions.txt"));
}

Analysis System::analyse(const std::string& question) const {
    Analysis analysis;
    analysis.question = question;
    analysis.tokens = tokenize(morph_, question);

    const Parser parser(grammar_);
    const auto started = std::chrono::steady_clock::now();
    const ParseResult forest = parser.parse(analysis.tokens);
    const auto finished = std::chrono::steady_clock::now();
    analysis.parse_microseconds =
        std::chrono::duration<double, std::micro>(finished - started).count();

    analysis.parsed = forest.accepted();
    analysis.derivations = forest.derivation_count();
    analysis.ambiguity_points = forest.ambiguity_points();
    analysis.forest_nodes = forest.node_count();
    analysis.chart_items = forest.item_count();
    if (!analysis.parsed) {
        analysis.failure = "no parse: the grammar does not describe this sentence";
        return analysis;
    }

    const Interpreter interpreter(grammar_, morph_, schema_, kb_);
    const std::vector<Tree> trees = forest.enumerate(kReadingLimit);

    std::map<std::string, int> seen;  // canonical form -> first reading that had it
    for (std::size_t i = 0; i < trees.size(); ++i) {
        const Interpretation interpretation = interpreter.interpret(trees[i], analysis.tokens);
        ReadingRecord record;
        record.tree = static_cast<int>(i);
        record.ok = interpretation.ok;
        record.reason = interpretation.reason;
        record.attachment_cost = interpretation.attachment_cost;
        if (interpretation.ok) {
            record.query = interpretation.query;
            record.logical_form = interpretation.query.str();
            record.canonical = interpretation.query.canonical();
            ++analysis.accepted_readings;
            const auto found = seen.find(record.canonical);
            if (found == seen.end()) {
                seen.emplace(record.canonical, static_cast<int>(i));
                ++analysis.distinct_forms;
            } else {
                record.duplicate_of_earlier = true;
            }
        }
        analysis.readings.push_back(std::move(record));
    }

    // The choice: only readings that mapped onto the model are eligible, and
    // among those the one whose prepositional phrases attach closest to what
    // they modify. That is the late closure preference, and it is applied after
    // the semantic filter, never before it.
    int best = -1;
    for (std::size_t i = 0; i < analysis.readings.size(); ++i) {
        const ReadingRecord& record = analysis.readings[i];
        if (!record.ok || record.duplicate_of_earlier) continue;
        if (best < 0 || record.attachment_cost < analysis.readings[static_cast<std::size_t>(best)]
                                                     .attachment_cost)
            best = static_cast<int>(i);
    }
    if (best < 0) {
        analysis.failure = "no reading of this sentence maps onto the knowledge model";
        return analysis;
    }
    analysis.chosen = best;

    const ReadingRecord& winner = analysis.readings[static_cast<std::size_t>(best)];
    analysis.choice_reason =
        std::to_string(analysis.derivations) + " derivation(s), " +
        std::to_string(analysis.accepted_readings) + " mapped onto the model, " +
        std::to_string(analysis.distinct_forms) + " distinct logical form(s); chose reading " +
        std::to_string(best + 1) + " with attachment cost " +
        std::to_string(winner.attachment_cost);
    if (analysis.distinct_forms > 1) {
        analysis.choice_reason += ", the closest attachment among the surviving readings";
    } else if (analysis.accepted_readings > 1) {
        analysis.choice_reason += "; the other readings differ only in the tree, not in the meaning";
    }

    analysis.sql = to_sql(schema_, winner.query);
    analysis.sparql = to_sparql(schema_, winner.query);
    analysis.paraphrase = paraphrase(schema_, morph_, kb_, winner.query);
    analysis.answer = evaluate(kb_, winner.query);
    return analysis;
}

}  // namespace lex
