#include <catch2/catch_test_macros.hpp>

#include "GameHarness.h"

#include <romanian_whist/strategies/common/RoundMemory.h>

#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

using namespace romanian_whist;
using namespace romanian_whist::test;

// What GoldenGameTests does for scores, this does for the shared round tracker.
//
// The two suites play the SAME games - same seed, same round-robin line-up - and
// differ only in what they measure, so a failure here and a pass next door means
// the change is in common::RoundMemory rather than in the engine.
//
// Everything asserted below is an integer or a bitmask. That is deliberate and
// it is the whole reason this file exists: the Reckoner score pin in
// ReckonerStrategyTests is float-derived and therefore only meaningful on one
// toolchain (see the comment there), which left the class that commit c831612
// actually extracted guarded only on Linux. These vectors hold everywhere.

namespace
{
constexpr std::uint32_t kGoldenSeed = 42; // the same games GoldenGameTests pins

// FNV-1a. Any stable hash would do; this one is four lines and has no
// dependencies, which matters more here than its collision behaviour - the
// input is a few hundred bytes of state we generated ourselves, not adversarial.
class Digest
{
public:
    void feed(std::uint64_t value)
    {
        for(int byte = 0 ; byte < 8 ; byte++)
        {
            state ^= (value >> (byte * 8)) & 0xFFULL;
            state *= 0x100000001B3ULL;
        }
    }

    // Hex, not the raw integer, because this is what a failure message shows and
    // what gets pasted back into the pin below.
    std::string hex() const
    {
        std::ostringstream out;
        out << "0x" << std::hex << state;
        return out.str();
    }

private:
    std::uint64_t state = 0xCBF29CE484222325ULL;
};

// Drives a bare common::RoundMemory off the engine's callbacks, mirroring the
// wiring in ReckonerStrategy::onRoundStarted..onTrickWon. No strategy, no knobs,
// no RNG - this is the "testable by driving it with bare method calls" claim in
// the RoundMemory header, made good.
//
// Catch2 assertions are NOT used inside the callbacks: they throw, and throwing
// out through GameEngine::run() would abandon the game mid-round and report the
// unwind rather than the problem. Failures are collected and asserted on after.
class MemoryWatcher : public IGameObserver
{
public:
    common::RoundMemory memory;
    Digest digest;
    unsigned int roundsSeen = 0;
    std::vector<std::string> problems;

    void onRoundStarted(const GameEngine& engine) override
    {
        memory.initRound(engine.getPlayerCount(),
                         engine.getCurrentRoundTrickCount(),
                         0, // this watcher is not seated; nothing below reads mySeat
                         engine.getRoundLeaderSeat().index,
                         engine.getCurrentTrumpCard());
    }

    void onBetPlaced(const GameEngine&, Seat seat, unsigned int bet) override
    {
        memory.recordBid(seat.index, bet);
    }

    void onCardPlayed(const GameEngine&, Seat seat, const Card& card) override
    {
        memory.recordCardPlayed(seat.index, card);
    }

    void onTrickWon(const GameEngine&, Seat winner, unsigned int) override
    {
        memory.recordTrickWon(winner.index);
    }

    void onRoundScored(const GameEngine& engine) override
    {
        roundsSeen++;
        checkAgainstEngine(engine);
        checkInternalConsistency();
        checkVoidInferences();
        foldIntoDigest();
    }

private:
    void note(const std::string& what)
    {
        // One line per problem, prefixed with the round, because a state machine
        // that goes wrong usually goes wrong in every round after the first.
        problems.push_back("round " + std::to_string(roundsSeen) + ": " + what);
    }

    // The memory is only worth anything if it agrees with the engine that is the
    // source of truth. This is what makes the digest below a pin on CORRECT
    // state rather than merely on current state.
    void checkAgainstEngine(const GameEngine& engine)
    {
        for(unsigned int p = 0 ; p < memory.playerCount ; p++)
        {
            const Seat seat{p};

            const auto bet = engine.getBet(seat);
            if(!bet)
            {
                note("engine has no bet for seat " + std::to_string(p));
            }
            else if(memory.bids[p] != static_cast<int>(*bet))
            {
                note("bid mismatch at seat " + std::to_string(p) + ": memory "
                     + std::to_string(memory.bids[p]) + " vs engine " + std::to_string(*bet));
            }

            if(memory.won[p] != engine.getTricksWon(seat))
            {
                note("tricks mismatch at seat " + std::to_string(p) + ": memory "
                     + std::to_string(memory.won[p]) + " vs engine " + std::to_string(engine.getTricksWon(seat)));
            }
        }
    }

    void checkInternalConsistency()
    {
        const unsigned int n = memory.playerCount;
        const unsigned int r = memory.roundTrickCount;

        if(memory.completedTricks.size() != r)
            note("completedTricks is " + std::to_string(memory.completedTricks.size())
                 + ", expected " + std::to_string(r));

        if(!memory.currentTrickCards.empty())
            note("currentTrickCards not cleared at round end");

        common::Mask union_ = 0;
        for(unsigned int p = 0 ; p < n ; p++)
        {
            if(memory.handSize[p] != 0)
                note("seat " + std::to_string(p) + " still holds " + std::to_string(memory.handSize[p]) + " cards");

            if(std::popcount(memory.playedBy[p]) != static_cast<int>(r))
                note("seat " + std::to_string(p) + " played " + std::to_string(std::popcount(memory.playedBy[p]))
                     + " cards, expected " + std::to_string(r));

            if((union_ & memory.playedBy[p]) != 0)
                note("seat " + std::to_string(p) + " played a card another seat also played");

            union_ |= memory.playedBy[p];
        }

        // `live` is "neither played nor turned up as trump" - so at round end it
        // is exactly the undealt remainder.
        const auto& profile = common::getDeckProfile(n);
        common::Mask expectedLive = profile.fullDeckMask & ~union_;
        if(memory.trumpCard)
            expectedLive &= ~common::cardBit(*memory.trumpCard);

        if(memory.live != expectedLive)
            note("live is wrong: " + std::to_string(memory.live) + " vs expected " + std::to_string(expectedLive));

        if(memory.getDeadCount() != static_cast<unsigned int>(std::popcount(expectedLive)))
            note("getDeadCount disagrees with live");
    }

    // Recomputes the two certain inferences straight off the trick history, with
    // an implementation that shares no code with the one under test. The rules:
    // a seat that did not follow the lead suit holds none of it, and if it did
    // not trump either, the must-trump rule proves it holds no trump.
    void checkVoidInferences()
    {
        const unsigned int n = memory.playerCount;
        const auto& profile = common::getDeckProfile(n);

        std::array<std::uint8_t, 6> expected{};

        const bool hasTrump = memory.trumpCard.has_value();
        const unsigned int trumpSuit = hasTrump ? common::cardSuit(*memory.trumpCard, profile.ranksPerSuit) : 0;

        for(const auto& trick : memory.completedTricks)
        {
            if(trick.cards.empty())
            {
                note("a completed trick has no cards");
                continue;
            }

            const unsigned int leadSuit = common::cardSuit(trick.cards.front().card, profile.ranksPerSuit);

            for(std::size_t i = 1 ; i < trick.cards.size() ; i++)
            {
                const auto& played = trick.cards[i];
                const unsigned int suit = common::cardSuit(played.card, profile.ranksPerSuit);

                if(suit == leadSuit)
                    continue;

                expected[played.seat] |= static_cast<std::uint8_t>(1U << leadSuit);

                if(hasTrump && suit != trumpSuit)
                    expected[played.seat] |= static_cast<std::uint8_t>(1U << trumpSuit);
            }
        }

        for(unsigned int p = 0 ; p < n ; p++)
        {
            if(memory.voidMask[p] != expected[p])
                note("voidMask at seat " + std::to_string(p) + " is " + std::to_string(memory.voidMask[p])
                     + ", independently recomputed as " + std::to_string(expected[p]));
        }
    }

    void foldIntoDigest()
    {
        digest.feed(memory.playerCount);
        digest.feed(memory.roundTrickCount);
        digest.feed(memory.opener);
        digest.feed(memory.live);
        digest.feed(memory.trumpCard ? *memory.trumpCard : 0xFFULL);
        digest.feed(memory.completedTricks.size());

        for(unsigned int p = 0 ; p < 6 ; p++)
        {
            digest.feed(memory.playedBy[p]);
            digest.feed(memory.handSize[p]);
            digest.feed(memory.voidMask[p]);
            digest.feed(static_cast<std::uint64_t>(memory.bids[p]));
            digest.feed(memory.won[p]);
        }

        for(const auto& trick : memory.completedTricks)
        {
            digest.feed(trick.leader);
            digest.feed(trick.winner);
            for(const auto& card : trick.cards)
            {
                digest.feed(card.seat);
                digest.feed(card.card);
            }
        }
    }
};

// Plays one scenario and hands back the watcher, having already asserted every
// invariant it collected.
MemoryWatcher watch(GameStructure structure, unsigned int playerCount)
{
    MemoryWatcher watcher;
    const auto engine = playFullGame(structure, buildRoundRobinProviders(playerCount),
                                     kGoldenSeed, { &watcher });

    INFO("problems: " << [&] {
        std::string joined;
        for(const auto& problem : watcher.problems)
            joined += "\n  " + problem;
        return joined;
    }());
    REQUIRE(watcher.problems.empty());
    REQUIRE(watcher.roundsSeen > 0);

    return watcher;
}
}

// The invariants above are what make these correct; the digest is what makes
// them a GUARD. Any change to what RoundMemory records - a different void
// inference, a card counted to the wrong seat, a reset that misses a field -
// moves the hash, including changes the invariants are too coarse to catch.
//
// A deliberate change SHOULD break this. Re-pin it in the same commit and say in
// the message what moved and why, exactly as for the score goldens.
//
// Unlike the Reckoner score pin, these hold on every platform: nothing folded
// into the digest is a float.
TEST_CASE("RoundMemory: pinned state across every deck profile, S_181", "[memory][golden]")
{
    CHECK(watch(GameStructure::S_181, 2).digest.hex() == "0x6707b2c23719a593");
    CHECK(watch(GameStructure::S_181, 3).digest.hex() == "0x16945d8c3a422f4a");
    CHECK(watch(GameStructure::S_181, 4).digest.hex() == "0x6090b17b22c355f1");
    CHECK(watch(GameStructure::S_181, 5).digest.hex() == "0xe915040a1e231a01");
    CHECK(watch(GameStructure::S_181, 6).digest.hex() == "0xd4ab2f5a7abf33db");
}

TEST_CASE("RoundMemory: pinned state across every deck profile, S_818", "[memory][golden]")
{
    CHECK(watch(GameStructure::S_818, 2).digest.hex() == "0xc164e5d1ffd0c50e");
    CHECK(watch(GameStructure::S_818, 3).digest.hex() == "0xd08b7c363b4a4b98");
    CHECK(watch(GameStructure::S_818, 4).digest.hex() == "0xb0039ac805920288");
    CHECK(watch(GameStructure::S_818, 5).digest.hex() == "0xe2c2812392ec365b");
    CHECK(watch(GameStructure::S_818, 6).digest.hex() == "0x15b3585608ef4d8e");
}

// The invariant that a bid of 0 does not look like an absent bid. Worth its own
// case because it is the one field where the natural `{}` is wrong, and because
// bidsPlaced() doubles as a seat's own position in the bidding order - so a
// default-constructed memory answering "4" would tell a seat about to bid that
// bidding was over.
TEST_CASE("RoundMemory: an unplaced bid is not a bid of zero", "[memory]")
{
    const common::RoundMemory fresh;

    for(unsigned int p = 0 ; p < 6 ; p++)
    {
        INFO("seat " << p);
        CHECK(fresh.bids[p] == common::UNBID);
    }

    CHECK(fresh.bidsPlaced() == 0);
    CHECK(fresh.previousBidSum() == 0);
}

TEST_CASE("RoundMemory: bids of 0 and UNBID stay distinguishable", "[memory]")
{
    common::RoundMemory memory;
    memory.initRound(4, 1, 0, 0, std::nullopt);

    CHECK(memory.bidsPlaced() == 0);

    memory.recordBid(0, 0);
    CHECK(memory.bids[0] == 0);
    CHECK(memory.bidsPlaced() == 1); // a bid of zero counts as placed
    CHECK(memory.previousBidSum() == 0);

    memory.recordBid(1, 1);
    CHECK(memory.bidsPlaced() == 2);
    CHECK(memory.previousBidSum() == 1);
}

// initRound() resets all six seats, not just the ones in play, so a memory
// reused for a smaller table does not report the previous round's bids for the
// seats that no longer exist.
TEST_CASE("RoundMemory: initRound clears the seats above playerCount", "[memory]")
{
    common::RoundMemory memory;

    memory.initRound(6, 1, 0, 0, std::nullopt);
    for(unsigned int p = 0 ; p < 6 ; p++)
        memory.recordBid(p, 1);

    memory.initRound(2, 1, 0, 0, std::nullopt);

    for(unsigned int p = 0 ; p < 6 ; p++)
    {
        INFO("seat " << p);
        CHECK(memory.bids[p] == common::UNBID);
        CHECK(memory.won[p] == 0);
        CHECK(memory.voidMask[p] == 0);
        CHECK(memory.playedBy[p] == 0);
    }

    CHECK(memory.bidsPlaced() == 0);
}

// The must-trump inference, as a single located fixture.
//
// The game-driven checker above already proves this across ten full games, by
// recomputing every void independently - but it proves it statistically, and a
// regression there surfaces as a digest mismatch that names nothing. These two
// name it. They are also the clearest statement of the rule the strategy leans
// on hardest: one discard can rule out two whole suits at once.
TEST_CASE("RoundMemory: an off-suit discard with no trump in it proves two voids",
          "[memory]")
{
    common::RoundMemory memory;
    memory.initRound(4, 4, 0, 0, Card{Rank::Nine, Suit::Spades}); // spades are trump

    // Seat 0 leads hearts; seat 1 throws a club.
    memory.recordCardPlayed(0, Card{Rank::Ten, Suit::Hearts});
    memory.recordCardPlayed(1, Card{Rank::Eight, Suit::Clubs});

    const auto hearts = static_cast<std::uint8_t>(1U << static_cast<unsigned int>(Suit::Hearts));
    const auto spades = static_cast<std::uint8_t>(1U << static_cast<unsigned int>(Suit::Spades));

    // No hearts, because they did not follow. And no trump either: the rules
    // would have FORCED the ruff, so declining to ruff proves they could not.
    CHECK(memory.voidMask[1] == (hearts | spades));

    // The leader reveals nothing - they chose the suit.
    CHECK(memory.voidMask[0] == 0);
}

TEST_CASE("RoundMemory: a ruff proves only the lead-suit void", "[memory]")
{
    common::RoundMemory memory;
    memory.initRound(4, 4, 0, 0, Card{Rank::Nine, Suit::Spades});

    memory.recordCardPlayed(0, Card{Rank::Ten, Suit::Hearts});
    memory.recordCardPlayed(1, Card{Rank::Seven, Suit::Spades}); // ruffs

    const auto hearts = static_cast<std::uint8_t>(1U << static_cast<unsigned int>(Suit::Hearts));

    // Out of hearts, obviously. But they clearly DO hold trumps, so the second
    // inference must not fire - asserting the exact mask, not just the bit.
    CHECK(memory.voidMask[1] == hearts);
}
