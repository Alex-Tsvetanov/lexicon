// Earley parser producing a packed parse forest.
//
// The chart holds items (rule, dot, start) deduplicated per end position. Every
// item carries a list of links, and one link is one way of reaching that item:
// the predecessor item, one dot to the left, plus the forest node that fills the
// symbol between them. An ambiguous sentence therefore does not multiply items,
// it multiplies links, which is what makes the forest packed rather than a list
// of trees. A forest node is identified by (symbol, start, end), so identical
// subphrases are shared between readings instead of being copied.
//
// The number of derivations is computed on the forest by dynamic programming, so
// counting ambiguity never enumerates the trees. Enumeration is a separate call
// with an explicit limit, because the number of trees can grow much faster than
// the forest that represents them.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lexicon/grammar.hpp"
#include "lexicon/morph.hpp"

namespace lex {

struct Token {
    std::string text;
    std::vector<Reading> readings;
};

struct Link {
    int predecessor = -1;  // item one dot to the left, -1 when the dot is at 0
    int child = -1;        // forest node filling the symbol
};

struct Item {
    int rule = 0;
    int dot = 0;
    int start = 0;
    int end = 0;
    std::vector<Link> links;
};

struct ForestNode {
    int symbol = -1;
    int start = 0;
    int end = 0;
    bool terminal = false;
    int token = -1;              // terminal nodes: index of the token
    int reading = -1;            // terminal nodes: index of the matching reading
    std::vector<int> items;      // non terminal nodes: completed items deriving it
};

// A single reading extracted from the forest.
struct Tree {
    int symbol = -1;
    int rule = -1;     // -1 for terminal leaves
    int token = -1;    // >= 0 for terminal leaves
    int reading = -1;
    std::vector<Tree> children;
};

class ParseResult {
public:
    bool accepted() const { return root_ >= 0; }
    int root() const { return root_; }
    const std::vector<ForestNode>& nodes() const { return nodes_; }
    const std::vector<Item>& items() const { return items_; }

    // Number of complete derivations, saturating at kCountLimit.
    std::uint64_t derivation_count() const { return derivations_; }
    static constexpr std::uint64_t kCountLimit = 1ULL << 62;

    // Places in the forest where more than one derivation meets: a node built by
    // more than one production, or an item reached in more than one way. This is
    // where the ambiguity actually lives, and it stays linear where the number of
    // trees does not.
    std::size_t ambiguity_points() const;

    // Size of the chart, reported by the benchmark.
    std::size_t item_count() const { return items_.size(); }
    std::size_t node_count() const { return nodes_.size(); }

    // Derivations of the whole sentence, at most `limit` of them.
    std::vector<Tree> enumerate(std::size_t limit) const;

    std::string describe_node(const Grammar& grammar, int node) const;

    friend class Parser;

private:
    std::vector<Item> items_;
    std::vector<ForestNode> nodes_;
    int root_ = -1;
    std::uint64_t derivations_ = 0;
    const Grammar* grammar_ = nullptr;
};

// Segmentation plus morphological analysis. Punctuation is dropped, a token of
// digits becomes a number, and a form the transducer does not know is passed to
// the guesser rather than rejected outright.
std::vector<Token> tokenize(const Morphology& morph, std::string_view text);

class Parser {
public:
    explicit Parser(const Grammar& grammar) : grammar_(&grammar) {}
    ParseResult parse(const std::vector<Token>& tokens) const;

private:
    const Grammar* grammar_;
};

}  // namespace lex
