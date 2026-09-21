#ifndef ROMANIAN_WHIST_CYBORG_KNOBS_H
#define ROMANIAN_WHIST_CYBORG_KNOBS_H

namespace romanian_whist::cyborg
{

// Tuning for CyborgStrategy. NOT a difficulty dial - Cyborg ships as one
// strength, with no RNG and no presets, and its determinism is what makes an
// exact-equality parity test possible at all. See §9 of
// docs/romanian-whist-cyborg-ai.md for the whole set and what each is worth.
//
// Only the knobs some code actually reads live here. The rest of §9 arrives
// with the rules that read it - a knob nobody reads is indistinguishable from
// one that is read and broken, and adding a field later is a one-line diff.
struct CyborgKnobs
{
    // §3.3. How often a player holding a card that beats yours, and a spare to
    // throw instead, declines the trick. Raise it against a table of duckers,
    // lower it against one that wants tricks.
    float duckPropensity = 0.60f;

    // §4.5. Value of a non-contiguous top-of-suit card in eight-trick rounds.
    float gapCredit = 0.75f;

    // §4.5. Value of a non-contiguous top-of-suit card when not the round leader.
    float gapCreditFollower = 0.60f;

    // §4.1. Charge the seats still to bid their fair share (R/N each) before
    // deciding whether to floor or ceil. On by default: measured against the
    // literal rule, it was a net gain across table sizes.
    bool useExpectedRemaining = true;

    // §4.5. Refinement for length credit in no-trump rounds.
    bool useLengthCredit = false;

    // §6.2. The minimum pHolds() worth paying for a trick with players still to
    // act. Below it, the cheapest card that beats the current winner is a card
    // spent on a trick somebody later will take anyway.
    float holdThreshold = 0.50f;

    // §5. How far the hand may drift from its plan before the mode changes -
    // below the bid into TAKE, above it into SHED.
    float slack = 0.75f;

    // Which engine gets first refusal at each decision.
    //
    // These are NOT a phase seam that later becomes dead code. The heuristics
    // are the strategy's degraded answer, and stay live in every configuration:
    // whenever the round memory disagrees with the position it was handed, both
    // decisions fall back to them rather than to something arbitrary. What these
    // flags change is only who is asked FIRST.
    //
    // Section 6 play took first refusal in Phase 3c, once the no-trump BALANCE
    // and SHED fixes brought it level with or ahead of heuristic play - see the
    // pinned A/Bs in tests/CyborgTournamentTests.cpp.
    bool useHeuristicBid = false;
    bool useHeuristicPlay = false;

    // The configuration under which CyborgStrategy is required to play a game
    // element-wise identically to LowRiskStrategy.
    //
    // Every field is set EXPLICITLY rather than left to the defaults, even where
    // the two currently agree. That is the whole point: when a later phase flips
    // a default, the parity test must keep pinning what it pins today rather
    // than quietly start testing the new path and failing as though behaviour
    // had regressed.
    static constexpr CyborgKnobs parityWithLowRisk()
    {
        CyborgKnobs knobs{};
        knobs.duckPropensity = 0.60f;
        knobs.gapCredit = 0.75f;
        knobs.gapCreditFollower = 0.60f;
        knobs.useExpectedRemaining = false;
        knobs.useLengthCredit = false;
        knobs.holdThreshold = 0.50f;
        knobs.slack = 0.75f;
        knobs.useHeuristicBid = true;
        knobs.useHeuristicPlay = true;
        return knobs;
    }
};

} // namespace romanian_whist::cyborg

#endif
