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

    // Which engine gets first refusal at each decision. Both true today because
    // the Cyborg rules for them are not written yet.
    //
    // These are NOT a phase seam that later becomes dead code. The heuristics
    // are the strategy's degraded answer, and stay live in every configuration:
    // whenever the round memory disagrees with the position it was handed, both
    // decisions fall back to them rather than to something arbitrary. What these
    // flags change is only who is asked FIRST.
    bool useHeuristicBid = true;
    bool useHeuristicPlay = true;

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
        knobs.useHeuristicBid = true;
        knobs.useHeuristicPlay = true;
        return knobs;
    }
};

} // namespace romanian_whist::cyborg

#endif
