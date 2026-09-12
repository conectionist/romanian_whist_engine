#include <romanian_whist/strategies/common/RoundMemory.h>

namespace romanian_whist::common
{

void RoundMemory::initRound(unsigned int n, unsigned int r, unsigned int mySeatIdx,
                            unsigned int openerSeat, std::optional<Card> trump)
{
    playerCount = n;
    roundTrickCount = r;
    mySeat = mySeatIdx;
    opener = openerSeat;
    currentTrickLeader = openerSeat;

    const auto& profile = getDeckProfile(playerCount);

    if(trump)
    {
        trumpCard = cardToId(*trump, playerCount);
        trumpSuit = trump->suit;
    }
    else
    {
        trumpCard = std::nullopt;
        trumpSuit = std::nullopt;
    }

    live = profile.fullDeckMask;
    if(trumpCard)
    {
        // The turned-up card is dead
        live &= ~cardBit(*trumpCard);
    }

    for(unsigned int p = 0; p < playerCount; ++p)
    {
        playedBy[p] = 0ULL;
        handSize[p] = roundTrickCount;
        voidMask[p] = 0;
        bids[p] = UNBID;
        won[p] = 0;
    }

    completedTricks.clear();
    currentTrickCards.clear();
}

void RoundMemory::recordBid(unsigned int seat, unsigned int value)
{
    if(seat < playerCount)
    {
        bids[seat] = static_cast<int>(value);
    }
}

bool RoundMemory::cardBeats(CardId candidate, CardId currentBest, Suit leadSuit) const
{
    const auto& profile = getDeckProfile(playerCount);
    const unsigned int candSuit = cardSuit(candidate, profile.ranksPerSuit);
    const unsigned int bestSuit = cardSuit(currentBest, profile.ranksPerSuit);
    const unsigned int candRank = cardRank(candidate, profile.ranksPerSuit);
    const unsigned int bestRank = cardRank(currentBest, profile.ranksPerSuit);

    const std::optional<unsigned int> trumpSuitIdx =
        trumpSuit ? std::optional<unsigned int>(static_cast<unsigned int>(*trumpSuit)) : std::nullopt;

    const bool candIsTrump = trumpSuitIdx && candSuit == *trumpSuitIdx;
    const bool bestIsTrump = trumpSuitIdx && bestSuit == *trumpSuitIdx;

    if(candIsTrump || bestIsTrump)
    {
        if(candIsTrump != bestIsTrump)
            return candIsTrump;
        return candRank > bestRank;
    }

    const unsigned int leadSuitIdx = static_cast<unsigned int>(leadSuit);
    const bool candIsLead = candSuit == leadSuitIdx;
    const bool bestIsLead = bestSuit == leadSuitIdx;

    if(candIsLead != bestIsLead)
        return candIsLead;

    if(candIsLead)
        return candRank > bestRank;

    // Two off-suit discards: earlier stays ahead
    return false;
}

PrePlayState RoundMemory::recordCardPlayed(unsigned int seat, const Card& card)
{
    PrePlayState before{};

    if(seat >= playerCount)
        return before;

    const CardId c = cardToId(card, playerCount);
    const auto& profile = getDeckProfile(playerCount);

    // Determine current best card and lead suit before this card is played
    before.bestHolderBefore = seat;

    if(!currentTrickCards.empty())
    {
        const CardId leadCard = currentTrickCards.front().card;
        before.leadSuit = static_cast<Suit>(cardSuit(leadCard, profile.ranksPerSuit));
        before.bestBefore = leadCard;
        before.bestHolderBefore = currentTrickCards.front().seat;

        for(std::size_t i = 1; i < currentTrickCards.size(); ++i)
        {
            if(cardBeats(currentTrickCards[i].card, before.bestBefore, *before.leadSuit))
            {
                before.bestBefore = currentTrickCards[i].card;
                before.bestHolderBefore = currentTrickCards[i].seat;
            }
        }
    }

    live &= ~cardBit(c);
    playedBy[seat] |= cardBit(c);
    if(handSize[seat] > 0)
    {
        handSize[seat]--;
    }

    // Hard void inferences: failing to follow proves a void in the lead suit,
    // and discarding off-suit without trumping proves a void in trump too,
    // because the must-trump rule would not have allowed the discard otherwise.
    if(before.leadSuit)
    {
        const unsigned int cSuit = cardSuit(c, profile.ranksPerSuit);
        const unsigned int lSuit = static_cast<unsigned int>(*before.leadSuit);

        if(cSuit != lSuit)
        {
            voidMask[seat] |= static_cast<std::uint8_t>(1 << lSuit);

            if(trumpSuit && cSuit != static_cast<unsigned int>(*trumpSuit))
            {
                voidMask[seat] |= static_cast<std::uint8_t>(1 << static_cast<unsigned int>(*trumpSuit));
            }
        }
    }

    currentTrickCards.push_back(TrickCard{seat, c});

    return before;
}

void RoundMemory::recordTrickWon(unsigned int winner)
{
    if(winner < playerCount)
    {
        won[winner]++;
        completedTricks.push_back(CompletedTrick{currentTrickLeader, winner, std::move(currentTrickCards)});
        currentTrickCards.clear();
        currentTrickLeader = winner;
    }
}

Mask RoundMemory::getUnseenCards(Mask myHand) const
{
    return live & ~myHand;
}

unsigned int RoundMemory::getDeadCount() const
{
    unsigned int inHands = 0;
    for(unsigned int p = 0; p < playerCount; ++p)
    {
        inHands += handSize[p];
    }
    const unsigned int liveCount = static_cast<unsigned int>(popcount(live));
    return (liveCount >= inHands) ? (liveCount - inHands) : 0;
}

unsigned int RoundMemory::bidsPlaced() const
{
    unsigned int placed = 0;
    for(unsigned int p = 0; p < playerCount; ++p)
    {
        if(bids[p] != UNBID)
        {
            placed++;
        }
    }
    return placed;
}

unsigned int RoundMemory::previousBidSum() const
{
    unsigned int sum = 0;
    for(unsigned int p = 0; p < playerCount; ++p)
    {
        if(bids[p] != UNBID)
        {
            sum += static_cast<unsigned int>(bids[p]);
        }
    }
    return sum;
}

unsigned int RoundMemory::handsOutstanding() const
{
    unsigned int outstanding = 0;
    for(unsigned int p = 0; p < playerCount; ++p)
    {
        if(p != mySeat)
        {
            outstanding += handSize[p];
        }
    }
    return outstanding;
}

} // namespace romanian_whist::common
