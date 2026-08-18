// Sentence synthesis: the logical form said back in English.
//
// The paraphrase is what the user confirms before anything is executed, so it
// has to be built from the logical form and not from the question. If it were
// built from the question it would agree with the user by construction and
// would confirm nothing.
//
// Inflection goes through the morphological transducer in the generation
// direction, so the plural of a noun and the participle of a verb come from the
// same lexicon the analyser used.
#pragma once
#include <string>

#include "lexicon/kb.hpp"
#include "lexicon/logic.hpp"
#include "lexicon/morph.hpp"
#include "lexicon/semantics.hpp"

namespace lex {

std::string paraphrase(const Schema& schema, const Morphology& morph, const KnowledgeBase& kb,
                       const Query& query);

}  // namespace lex
