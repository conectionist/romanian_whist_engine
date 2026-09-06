#ifndef ROMANIAN_WHIST_EVALUATOR_H
#define ROMANIAN_WHIST_EVALUATOR_H

#include <romanian_whist/strategies/reckoner/CardMask.h>

#include <array>
#include <optional>

namespace romanian_whist::reckoner
{

inline constexpr float CATCH = 0.7f;
inline constexpr float RUFF_W = 0.4f;
inline constexpr float LONG_W = 0.5f;

struct EvalContext
{
    unsigned int playerCount = 4;
    Mask live = 0;
    std::array<unsigned int, 6> handSize{};
    std::array<std::uint8_t, 6> voidMask{}; // bit s is (1 << s)
    std::optional<Suit> trumpSuit = std::nullopt;
};

// Hypergeometric P(h cards drawn without replacement from n contain none of k marked cards)
float hyper0(unsigned int k, unsigned int n, unsigned int h);

// Fast round-shape-aware estimate of hand H's expected tricks
float evalHand(Mask hand, unsigned int seat, const EvalContext& ctx);

} // namespace romanian_whist::reckoner

#endif
