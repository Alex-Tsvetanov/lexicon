// The demo. It prints every stage of the pipeline for one question or for the
// whole bundled question set, because showing the stages is the point: a system
// that only printed the final query would be indistinguishable from a template.
#include <iostream>
#include <string>
#include <vector>

#include "lexicon/pipeline.hpp"

namespace {

void print_rule(char fill = '=') { std::cout << std::string(78, fill) << "\n"; }

void indent_block(const std::string& text, const std::string& prefix) {
    std::string line;
    for (const char c : text) {
        if (c == '\n') {
            std::cout << prefix << line << "\n";
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) std::cout << prefix << line << "\n";
}

void show(const lex::System& system, const std::string& question, bool verbose) {
    const lex::Analysis analysis = system.analyse(question);

    print_rule();
    std::cout << "QUESTION  " << analysis.question << "\n";
    print_rule('-');

    std::cout << "1. TOKENS AND MORPHOLOGY\n";
    for (std::size_t i = 0; i < analysis.tokens.size(); ++i) {
        const lex::Token& token = analysis.tokens[i];
        std::cout << "   [" << i << "] " << token.text << "\n";
        for (const lex::Reading& reading : token.readings)
            std::cout << "        " << reading.str() << "\n";
    }

    std::cout << "\n2. PARSE FOREST\n";
    std::cout << "   derivations       " << analysis.derivations << "\n";
    std::cout << "   forest nodes      " << analysis.forest_nodes << "\n";
    std::cout << "   chart items       " << analysis.chart_items << "\n";
    std::cout << "   ambiguity points  " << analysis.ambiguity_points << "\n";
    std::cout << "   parse time        " << analysis.parse_microseconds << " us\n";
    if (!analysis.parsed) {
        std::cout << "\n   " << analysis.failure << "\n";
        return;
    }

    std::cout << "\n3. READINGS\n";
    for (std::size_t i = 0; i < analysis.readings.size(); ++i) {
        const lex::ReadingRecord& record = analysis.readings[i];
        std::cout << "   reading " << (i + 1) << ": ";
        if (!record.ok) {
            std::cout << "rejected, " << record.reason << "\n";
            continue;
        }
        std::cout << (record.duplicate_of_earlier ? "same meaning as an earlier reading"
                                                  : "kept")
                  << ", attachment cost " << record.attachment_cost << "\n";
        std::cout << "      " << record.logical_form << "\n";
    }
    if (analysis.chosen < 0) {
        std::cout << "\n   " << analysis.failure << "\n";
        return;
    }
    std::cout << "\n4. CHOSEN READING\n   " << analysis.choice_reason << "\n";

    std::cout << "\n5. LOGICAL FORM\n   "
              << analysis.readings[static_cast<std::size_t>(analysis.chosen)].logical_form << "\n";

    std::cout << "\n6. SQL\n";
    indent_block(analysis.sql, "   ");

    std::cout << "\n7. SPARQL\n";
    indent_block(analysis.sparql, "   ");

    std::cout << "\n8. PARAPHRASE BACK TO ENGLISH\n   " << analysis.paraphrase << "\n";
    std::cout << "\n9. ANSWER FROM THE BUNDLED KNOWLEDGE BASE\n   " << analysis.answer.str()
              << "\n";
    if (verbose) std::cout << "\n";
}

void show_bulgarian() {
    // The Bulgarian lexicon is a small demonstration that the transducer is not
    // tied to English: only the data changes.
    const lex::Morphology morph = lex::Morphology::load(lex::data_path("lexicon_bg.txt"));
    print_rule();
    std::cout << "BULGARIAN MORPHOLOGY (analysis only, the parser is English)\n";
    print_rule('-');
    for (const char* form : {"филм", "филмът", "филми", "филмите", "актьорите", "държави",
                             "държавата", "режисьор"}) {
        std::cout << "   " << form << "\n";
        for (const lex::Reading& reading : morph.analyze(form))
            std::cout << "        " << reading.str() << "\n";
    }
    const auto generated = morph.generate("актьор", "noun", lex::Features::parse("num=pl def=yes"));
    std::cout << "   generation: актьор + [num=pl def=yes] -> "
              << (generated.has_value() ? *generated : std::string("(none)")) << "\n";
}

int usage() {
    std::cout << "usage: lexicon [--all | --question \"...\" | --emit-sql-schema | --emit-turtle\n"
                 "               | --bulgarian | --machine]\n";
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const lex::System system = lex::System::load();

        if (args.empty() || args[0] == "--all") {
            for (const std::string& question : lex::System::question_set())
                show(system, question, true);
            show_bulgarian();
            return 0;
        }
        if (args[0] == "--question") {
            if (args.size() < 2) return usage();
            show(system, args[1], false);
            return 0;
        }
        if (args[0] == "--bulgarian") {
            show_bulgarian();
            return 0;
        }
        if (args[0] == "--emit-sql-schema") {
            std::cout << lex::sql_schema_and_data(system.schema(), system.knowledge());
            return 0;
        }
        if (args[0] == "--emit-turtle") {
            std::cout << lex::turtle_data(system.schema(), system.knowledge());
            return 0;
        }
        if (args[0] == "--machine") {
            // A flat format for tools/check_sql.py: one record per question.
            for (const std::string& question : lex::System::question_set()) {
                const lex::Analysis analysis = system.analyse(question);
                std::cout << "@question " << question << "\n";
                if (!analysis.answered()) {
                    std::cout << "@failed " << analysis.failure << "\n@end\n";
                    continue;
                }
                std::cout << "@answer " << analysis.answer.str() << "\n@sql\n"
                          << analysis.sql << "\n@endsql\n@end\n";
            }
            return 0;
        }
        return usage();
    } catch (const std::exception& error) {
        std::cerr << "lexicon: " << error.what() << "\n";
        return 1;
    }
}
