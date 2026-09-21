cmake_minimum_required(VERSION 3.21)

project(RomanianWhistEngine
    VERSION 4.2.0
    DESCRIPTION "Reusable Romanian Whist game engine"
    LANGUAGES CXX)

add_library(romanian_whist_engine STATIC
    src/Card.cpp
    src/CardValidator.cpp
    src/Deck.cpp
    src/Player.cpp
    src/PlayerList.cpp
    src/Trick.cpp
    src/Round.cpp
    src/Scoreboard.cpp
    src/GameEngine.cpp
    src/AiMoveProvider.cpp
    src/detail/RandomDraw.cpp
    src/strategies/FirstCardStrategy.cpp
    src/strategies/RandomCardStrategy.cpp
    src/strategies/TrickHeuristics.cpp
    src/strategies/LowRiskStrategy.cpp
    src/strategies/DuckingStrategy.cpp
    src/strategies/common/Hypergeometric.cpp
    src/strategies/common/RoundMemory.cpp
    src/strategies/reckoner/Evaluator.cpp
    src/strategies/reckoner/ReckonerTracker.cpp
    src/strategies/reckoner/Sampler.cpp
    src/strategies/reckoner/BasePolicy.cpp
    src/strategies/reckoner/RolloutEngine.cpp
    src/strategies/cyborg/Odds.cpp
    src/strategies/cyborg/Bidding.cpp
    src/strategies/cyborg/Plan.cpp
    src/strategies/cyborg/Play.cpp
    src/strategies/CyborgStrategy.cpp
    src/strategies/ReckonerStrategy.cpp
)

# Consumers link against this alias, so the name is identical whether the engine
# arrives via add_subdirectory() or a future find_package().
add_library(RomanianWhist::engine ALIAS romanian_whist_engine)

target_compile_features(romanian_whist_engine PUBLIC cxx_std_20)

# NO FLOATING-POINT CONTRACTION. Left to itself a compiler may fuse `a + b * c`
# into one fused multiply-add, which rounds once instead of twice - and the ARM
# toolchains do so by default. Cyborg's card play breaks ties between scores that
# agree to the last bit, so one differently rounded sum changes which card is
# played and the game diverges from there: the Phase 3b tournament pins moved on
# macOS/arm64 while Linux and Windows matched them exactly. With contraction off
# the engine computes the same floats on every platform. (MSVC does not contract
# under its default /fp:precise.) PRIVATE, so a consumer's own code keeps
# whatever its build chose.
target_compile_options(romanian_whist_engine PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/romanian_whist/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/romanian_whist/Version.h
    @ONLY)

target_include_directories(romanian_whist_engine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

option(WHIST_BUILD_TESTS "Build the engine's test suite" ${PROJECT_IS_TOP_LEVEL})

if(WHIST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
