// The whole pipeline in one place: tokens, morphology, forest, readings,
// logical form, two target languages, paraphrase and answer.
//
// Nothing here decides anything by itself. It calls the layers in order and
// keeps every intermediate result, because the demo has to be able to show each
// stage and the benchmark has to be able to measure them separately.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "lexicon/codegen.hpp"
#include "lexicon/earley.hpp"
#include "lexicon/kb.hpp"
#include "lexicon/semantics.hpp"
#include "lexicon/synth.hpp"

namespace lex {

struct ReadingRecord {
    int tree = -1;
    bool ok = false;
    std::string reason;       // why it was rejected
    std::string logical_form;
    std::string canonical;
    int attachment_cost = 0;
    bool duplicate_of_earlier = false;
    Query query;
};

struct Analysis {
    std::string question;
    std::vector<Token> tokens;

    bool parsed = false;
    std::uint64_t derivations = 0;
    std::size_t ambiguity_points = 0;
    std::size_t forest_nodes = 0;
    std::size_t chart_items = 0;
    double parse_microseconds = 0.0;

    std::vector<ReadingRecord> readings;
    std::size_t accepted_readings = 0;  // readings that mapped onto the model
    std::size_t distinct_forms = 0;     // distinct logical forms among them
    int chosen = -1;                    // index into readings
    std::string choice_reason;
    std::string failure;  // set when nothing survived

    std::string sql;
    std::string sparql;
    std::string paraphrase;
    Answer answer;

    bool answered() const { return chosen >= 0; }
};

class System {
public:
    static System load();
    static System load(const std::string& grammar_file);

    Analysis analyse(const std::string& question) const;

    const Morphology& morphology() const { return morph_; }
    const Grammar& grammar() const { return grammar_; }
    const Schema& schema() const { return schema_; }
    const KnowledgeBase& knowledge() const { return kb_; }

    // The frozen question set bundled with the repository.
    static std::vector<std::string> question_set();

    // Enumerating readings is capped: the forest can hold more derivations than
    // it is useful to interpret, and the cap is reported rather than hidden.
    static constexpr std::size_t kReadingLimit = 64;

private:
    Morphology morph_;
    Grammar grammar_;
    Schema schema_;
    KnowledgeBase kb_;
};

}  // namespace lex
