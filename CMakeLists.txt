cmake_minimum_required(VERSION 3.20)
project(lexicon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(lexicon_core
  src/features.cpp
  src/morph.cpp
  src/grammar.cpp
  src/earley.cpp
  src/logic.cpp
  src/semantics.cpp
  src/codegen.cpp
  src/synth.cpp
  src/kb.cpp
  src/pipeline.cpp
)
target_include_directories(lexicon_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(lexicon_core PUBLIC
  LEXICON_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Warnings are on for every target, and none of them is silenced by a pragma.
function(lexicon_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()
lexicon_warnings(lexicon_core)

# On MinGW the runtime DLLs are easy to shadow: any other toolchain earlier on
# PATH lends its own libstdc++ to the process and the result is a crash before
# main. Linking the runtime statically keeps the built binaries self contained.
if(MINGW)
  target_link_options(lexicon_core INTERFACE -static-libstdc++ -static-libgcc)
endif()

add_executable(lexicon apps/demo.cpp)
target_link_libraries(lexicon PRIVATE lexicon_core)
lexicon_warnings(lexicon)

add_executable(lexicon_bench apps/bench.cpp)
target_link_libraries(lexicon_bench PRIVATE lexicon_core)
lexicon_warnings(lexicon_bench)

enable_testing()

add_executable(lexicon_tests
  tests/check.cpp
  tests/test_morph.cpp
  tests/test_parse.cpp
  tests/test_semantics.cpp
  tests/test_codegen.cpp
  tests/test_pipeline.cpp
)
target_link_libraries(lexicon_tests PRIVATE lexicon_core)
lexicon_warnings(lexicon_tests)

# One CTest entry per test case, so a failure names the case that broke.
set(LEXICON_TEST_CASES
  morph_noun_singular_and_plural
  morph_orthographic_rules
  morph_verb_paradigm
  morph_homonymy_films
  morph_generation_roundtrip
  morph_unknown_word_guess
  morph_bulgarian_definite_article
  grammar_loads_and_indexes
  earley_accepts_simple_question
  earley_rejects_ungrammatical
  earley_counts_attachment_ambiguity
  earley_forest_is_shared
  earley_enumerates_distinct_trees
  semantics_type_check_prunes_reading
  semantics_ambiguous_question_two_forms
  semantics_negation
  semantics_count_query
  semantics_universal_quantifier
  semantics_duplicate_forms_collapse
  codegen_sql_projection_and_join
  codegen_sql_negation
  codegen_sparql_triples
  codegen_both_targets_same_form
  synth_paraphrase_mentions_relations
  kb_evaluates_conjunction
  kb_evaluates_negation_and_count
  kb_evaluates_universal
  pipeline_end_to_end_answers
  pipeline_reports_ambiguity
  pipeline_coverage_over_question_set
)
foreach(case IN LISTS LEXICON_TEST_CASES)
  add_test(NAME ${case} COMMAND lexicon_tests ${case})
endforeach()

add_custom_target(demo
  COMMAND $<TARGET_FILE:lexicon> --all
  DEPENDS lexicon
  USES_TERMINAL
  COMMENT "Running the Lexicon demo over the bundled question set")
