include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.0
)
FetchContent_MakeAvailable(Catch2)

add_executable(romanian_whist_engine_tests
    GameHarness.cpp
    CancellationTests.cpp
    MoveValidationTests.cpp
    CardValidatorTests.cpp
    DeckTests.cpp
    GameEngineTests.cpp
    ObserverTests.cpp
    ScoreboardTests.cpp
    TrickTests.cpp
    GoldenGameTests.cpp
    RandomPlayPropertyTests.cpp
    RoundHistoryTests.cpp
    TrickHeuristicsTests.cpp
    LowRiskStrategyTests.cpp
    AiMoveProviderTests.cpp
    ReadmeQuickStartTests.cpp
    CardMaskTests.cpp
    EvaluatorTests.cpp
    ReckonerTrackerTests.cpp
    SamplerTests.cpp
    BasePolicyTests.cpp
    ReckonerStrategyTests.cpp
    ReckonerTournamentTests.cpp
    ReckonerRobustnessTests.cpp
)

target_link_libraries(romanian_whist_engine_tests PRIVATE
    RomanianWhist::engine
    Catch2::Catch2WithMain
)

target_include_directories(romanian_whist_engine_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(romanian_whist_engine_tests PRIVATE cxx_std_20)

# Cards are compared by value, and half of those comparisons are on an optional
# (trump, getWinningCard(), everything TrickHeuristics returns). Without this
# Catch2 renders them "{?}" whatever StringMaker specializations are in scope.
# Set here rather than in a header because it must be seen before Catch2's
# catch_tostring.hpp, which catch_test_macros.hpp already includes.
target_compile_definitions(romanian_whist_engine_tests PRIVATE
    CATCH_CONFIG_ENABLE_OPTIONAL_STRINGMAKER
)

# ReadmeQuickStartTests compares the snippet printed in README.md against the
# copy it compiles, so it needs both files at run time. Tests are only built
# when this project is top level, so these always point at the source tree the
# build was configured from.
target_compile_definitions(romanian_whist_engine_tests PRIVATE
    WHIST_README_PATH="${PROJECT_SOURCE_DIR}/README.md"
    WHIST_QUICKSTART_TEST_PATH="${CMAKE_CURRENT_SOURCE_DIR}/ReadmeQuickStartTests.cpp"
)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(romanian_whist_engine_tests)
