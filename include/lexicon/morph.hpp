// Morphological analyser built as a finite state transducer.
//
// The transducer is the composition of two relations. The first is the lexicon,
// a set of stems each labelled with a lemma, a part of speech and a paradigm.
// The second is the affix relation of that paradigm, a list of forms of the shape
// "delete n bytes from the stem, then append this suffix", together with the
// features the affix contributes. The composition is performed once at load time
// and the result is stored as a deterministic acyclic transducer over bytes:
// every path spells one surface form and every accepting state carries the
// analyses of that form.
//
// The same composition is kept in the opposite direction, so generation, which
// the sentence synthesiser needs, shares one resource with the analyser.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lexicon/features.hpp"

namespace lex {

// One LEX line of a lexicon file.
struct LexEntry {
    std::string stem;
    std::string lemma;
    std::string pos;
    std::string paradigm;
    Features sem;  // type=Film, rel=directed, ent=p_nolan, det=every, ...
};

// One analysis of one surface form.
struct Reading {
    std::string lemma;
    std::string pos;
    Features feats;
    int entry = -1;  // index into Morphology::entries(), -1 when guessed
    bool guessed = false;

    std::string str() const;
    bool operator==(const Reading& other) const;
};

class Morphology {
public:
    static Morphology load(const std::string& path);

    // All analyses of a surface form, in lexicon order. Empty when the form is
    // not in the compiled transducer.
    std::vector<Reading> analyze(std::string_view form) const;

    // Analyses of an unknown form, taken from the longest affix that some
    // paradigm uses. Called only when analyze() returns nothing.
    std::vector<Reading> guess(std::string_view form) const;

    // Inverse direction: the surface form of a lemma whose features include all
    // the requested ones. Empty when the lexicon has no such form.
    std::optional<std::string> generate(const std::string& lemma, const std::string& pos,
                                        const Features& wanted) const;

    const std::vector<LexEntry>& entries() const { return entries_; }
    const LexEntry* entry(int index) const;

    // Size of the compiled transducer, reported by the demo and the benchmark.
    std::size_t state_count() const { return states_.size(); }
    std::size_t edge_count() const;
    std::size_t form_count() const { return form_count_; }
    std::size_t lexeme_count() const { return entries_.size(); }

private:
    struct State {
        std::vector<std::pair<char, int>> edges;
        std::vector<int> outputs;  // indices into outputs_
    };
    struct GenForm {
        std::string lemma;
        std::string pos;
        Features feats;
        std::string surface;
    };
    struct AffixHint {
        std::string suffix;
        std::string pos;
        Features feats;
    };

    int add_path(std::string_view surface);
    void add_form(const std::string& surface, const Reading& reading);

    std::vector<State> states_{State{}};
    std::vector<Reading> outputs_;
    std::vector<LexEntry> entries_;
    std::vector<GenForm> generation_;
    std::vector<AffixHint> hints_;
    std::size_t form_count_ = 0;
};

}  // namespace lex
