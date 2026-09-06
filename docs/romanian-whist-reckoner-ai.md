# "Reckoner" — a tracking + Monte-Carlo AI for Romanian Whist

Design goals, in priority order: (1) consistently beat LowRisk, (2) be a hard opponent for a skilled
human, (3) decide in well under 100 ms per move on one core. Everything below is meant to be
implementable directly from the pseudocode. Numbers that are guesses rather than derivations are
listed as knobs in §9.

## 0. Architecture

Five components, layered:

| Component | Role | Cost |
|---|---|---|
| **Tracker** (§1) | Bitmask memory of the round: cards played and by whom, the turned-up trump card (dead), known voids (incl. trump voids implied by the must-trump rule), bids/tricks, and soft "rank-gap" constraints inferred from how each opponent played | O(1) per observed card |
| **Evaluator** (§2) | Fast, round-shape-aware estimate of a hand's expected tricks | ~0.1–0.3 µs |
| **Sampler** (§3) | Draws plausible full deals of the unseen cards consistent with the Tracker, weighted by bid plausibility and play consistency | ~3–5 µs per deal |
| **Base policy** (§4) | Rule-based target-hitting player: DUCK / TAKE / BALANCE modes, sure-win logic, 1-ply "alignment" lookahead. Runs inside every simulation; also the fallback move | ~0.3–1 µs per decision |
| **Decision layer** (§5, §6) | Bidding and card play by Monte-Carlo rollouts: for each candidate (bid or card), play out the rest of the round over many sampled deals with everyone using the base policy; pick the candidate maximizing expected score (own score + streak bonus − λ·opponents' scores) | 1–50 ms per play, 20–80 ms per bid |

The base policy alone already beats LowRisk (memory, sure-win/sure-lose logic, and real
exact-hit decisions). The MC layer adds lookahead and opponent modeling and is what makes it hard
for a human.

The method is "determinization" (perfect-information Monte Carlo, as in Bridge and Skat programs).
Its known bias — strategy fusion, i.e. acting as if you will know the deal later — is kept small by
using an information-limited rollout policy instead of a double-dummy solver, and by never
searching more than a few tricks deep exactly.

---

## 1. Representation and Tracker

### 1.1 Cards as bitmasks

```
D = deck size (8·N for N players).  card id c in [0, D).
suit(c) = c / RANKS,  rank(c) = c % RANKS    (higher rank = stronger)
Mask = uint64_t

Precomputed masks:
  SUIT[s]        all cards of suit s
  ABOVE[c]       cards of suit(c) with rank > rank(c)
  BELOW[c]       cards of suit(c) with rank < rank(c)
  BETWEEN(a,b)   = ABOVE[a] & BELOW[b]   (same suit, a lower than b)
```

Every set the AI reasons about (hand, live cards, unseen cards, "higher cards of this suit still
out") is a mask, so every question in §2–§4 is an AND plus a popcount.

### 1.2 Tracker state (per round)

```
N, R (round size), trumpSuit (or NONE), trumpCard (or NONE)
seatOrder[], opener (leads trick 1), bid[p] (or UNBID), won[p]
live        : Mask   // not yet played and not the turned-up card: could be in a hand or in the dead pile
playedBy[p] : Mask   // cards p has played this round (for end-of-round reconstruction)
handSize[p] : int    // cards p still holds (already decremented for the current trick)
void[p]     : 4 bits // suits p is known not to hold
constraints : list of (p, excluded : Mask, conf : float)   // "p holds none of `excluded`", believed with prob conf
trick log   : leader, cards in order, winner — for each completed trick and the current one
deadCount   = popcount(live) − Σ_p handSize[p]   // undealt cards; constant through the round, 0 in 8-trick rounds
```

Derived, from my seat: `unseen = live & ~myHand`. Note that `live` excludes the turned-up card,
so its rank feeds straight into every "top remaining card" test: a turned-up trump ace makes your
trump king the master trump for the whole round, a turned-up side-suit ace makes your king in
that suit top. LowRisk ignores the turn-up's rank entirely.

In 8-trick rounds `deadCount = 0`, so every unseen card is definitely in someone's hand; the
Tracker's inferences are strongest exactly where the biggest scores are.

### 1.3 Update rules and hard inferences

On every observed play `(p, c)` in a trick with lead suit `L` (NONE if `p` led) and current best
card `B` (before `p` played):

```
live &= ~bit(c);  playedBy[p] |= bit(c);  handSize[p]--
if L != NONE and suit(c) != L:
    void[p] |= L                                     // failed to follow
    if trumpSuit != NONE and suit(c) != trumpSuit:
        void[p] |= trumpSuit                         // must-trump rule: a non-trump discard proves no trump
```

The must-trump inference is a big deal: one off-suit discard tells you a player has neither the
lead suit nor any trump. That instantly turns many "uncertain" cards in your hand into sure
winners or sure losers for the rest of the round.

### 1.4 Soft inferences from how opponents play ("rank-gap constraints")

Any sane opponent (human, LowRisk, Ducking) plays in one of two modes at a given moment:
"wants this trick" or "doesn't". The card they choose then reveals a rank interval they do not
hold. Record these as soft constraints the Sampler penalizes (§3), never as hard facts.

Model of opponent `p` at observation time: `need = bid[p] − won[p]`, `rem` = tricks remaining
including the current one.
- `need <= 0` → assume DUCK (this covers bid-0 players and players who are already over).
- `need >= rem` → assume TAKE.
- otherwise → BALANCE: unpredictable; use low confidence (or skip).

When `p` follows suit (`suit(c) == L`), with `B` the current best card:

| Situation | Inference (excluded set for `p`) | conf |
|---|---|---|
| DUCK, c does not beat B, B is in suit L | no L-card with rank in (rank(c), rank(B)) — they dump the *highest* loser | 0.80 |
| DUCK, c does not beat B, B is a trump | no L-card above rank(c) | 0.80 |
| DUCK, c beats B (forced win) | no L-card below rank(B) (they had no loser) and none above rank(c) (they shed their highest) | 0.85 |
| TAKE, c does not beat B, B in suit L | no L-card above rank(B) (they would have won if they could) | 0.90 |
| TAKE, c beats B | no L-card with rank in (rank(B), rank(c)) — they win as cheaply as possible | 0.70 |
| BALANCE, either | same intervals as above depending on win/lose | 0.35 |

When `p` ruffs (`suit(c) == trumpSuit != L`) and another trump `T` is already the best card:
DUCK and c < T (undertrump): no trump in (rank(c), rank(T)); TAKE and c > T: no trump in
(rank(T), rank(c)). Same confidences.

Store each as `(p, excludedMask, conf)`. Against a deterministic AI opponent whose policy you
ship (LowRisk, Ducking) use conf ≈ 0.98 — their plays are literally these rules, so the Sampler
ends up knowing much of their hand.

### 1.5 End-of-round reconstruction and calibration (cross-round opponent model)

When a round ends, every card has been seen, so each opponent's *starting* hand is
`playedBy[o]` exactly. Use it to calibrate their bidding:

```
E_o = evalHand(playedBy[o], o, initialContext)     // §2, evaluated as at bid time
r_o = bid[o] − E_o
update running mean bias[o] and std sigma[o] with a prior (bias 0, sigma 1.2, pseudo-count 3)
```

`bias[o]` ("this human under-bids by 0.6") and `sigma[o]` ("this one is erratic") sharpen the
bid-plausibility weights next round (§3). Keep them across the whole game.

### 1.6 Hooks you need to plumb

The play interface only shows the current trick, so cards played *after* your card in a trick
would never be seen. You need:

- `onRoundStart(N, R, trumpCard, seatOrder, opener)` — also tells the AI the deck composition.
- `onBid(seat, value)` for every player, and its own streak counters (hits/misses in a row,
  excluding 1-trick rounds).
- `onCardPlayed(seat, card)` or at minimum `onTrickComplete(leaderSeat, cards[])`.
- `onRoundEnd()` for §1.5, and current standings (for sabotage weighting, §5.3).

---

## 2. Evaluator: expected tricks of a hand

Used for: bid plausibility of sampled opponent hands, guessing not-yet-declared bids inside
bidding rollouts, and the BALANCE alignment rule. It is a prior; the rollouts do the precise
work, so only its rough calibration and ordering matter.

```
hyper0(k, n, h):        // P(h cards drawn without replacement from n contain none of k marked cards)
    if n − k < h: return 0
    p = 1
    for i in 0..h−1: p *= (n − k − i) / (n − i)
    return p

evalHand(H, p, ctx):    // ctx = {live, handSize[], void[], trumpSuit}; H = p's current hand
    U = live & ~H;  n = popcount(U)
    others = Σ_{q≠p} handSize[q]
    q = others / n                          // chance an unknown card sits in an opponent's hand, not the dead pile
    E = 0
    for c in H:
        hi   = popcount(U & ABOVE[c])       // higher cards of this suit unaccounted for
        pTop = (1 − q·CATCH)^hi             // CATCH ≈ 0.7: a higher card that IS out is actually used against c
        if trumpSuit == NONE or suit(c) == trumpSuit:
            pW = pTop
        else:
            nS = popcount(U & SUIT[suit(c)]);  nT = popcount(U & SUIT[trumpSuit])
            pRuff = 0
            if nT > 0:
                for each opponent o with handSize[o] > 0 and not void[o][trumpSuit]:
                    pVoid = void[o][suit(c)] ? 1 : hyper0(nS, n, handSize[o])
                    pHasT = 1 − hyper0(nT, n, handSize[o])
                    pRuff = 1 − (1 − pRuff)·(1 − pVoid·pHasT)
            pW = pTop · (1 − pRuff)
        E += pW
    if trumpSuit != NONE:
        myT  = popcount(H & SUIT[trumpSuit]);  oppT = q · popcount(U & SUIT[trumpSuit])
        short = number of non-trump suits with popcount(H & SUIT[s]) <= 1 and popcount(U & SUIT[s]) > 0
        E += RUFF_W · min(myT, short)        // ruffing tricks,      RUFF_W ≈ 0.4
        E += LONG_W · max(0, myT − oppT − 1) // long-trump tricks,   LONG_W ≈ 0.5
    return clamp(E, 0, popcount(H))
```

Why this beats a static count: `pRuff` is computed from *this round's* hand sizes, so a bare ace
is worth ~1.0 tricks in an 8-trick (no-trump) round, ~0.8 in a 7-trick round, and only ~0.4 in a
2-trick round with 4 players (each opponent has a 57% chance of being void in your suit and a
43% chance of holding a trump). `q` handles the dead pile: in a 3-trick, 4-player round only 9
of the 28 unseen cards are actually in hands, so a higher card "still out" is usually not a
threat. LowRisk's ace-counting gets both of these wrong, and it overbids short rounds
systematically.

---

## 3. Sampler: plausible deals of the unseen cards

A "sample" assigns every unseen card either to an opponent's hand (respecting each hand size and
all known voids) or to the dead pile.

```
drawSamples(K):
    U = list of cards in (live & ~myHand)
    cap[o] = handSize[o] for each opponent o;  dead = |U| − Σ cap
    order U so the most constrained suits come first (suits that more players are void in),
    random order within a suit
    samples = []
    repeat K times:
        hand[o] = 0 ∀o;  cap' = cap;  deadLeft = dead
        for c in U (order above):
            elig = { o : cap'[o] > 0 and not void[o][suit(c)] }  ∪  { DEAD if deadLeft > 0 }
            if elig is empty: restart this sample (after 50 restarts: ignore voids for this sample and
                              log it — contradictory hard constraints mean a rules bug)
            pick o in elig with probability ∝ remaining capacity (cap'[o], or deadLeft)
            assign c to o
        w = 1
        for each opponent o:
            startHand = hand[o] | playedBy[o]                       // its full starting hand under this sample
            if bid[o] != UNBID: w *= bidLik(o, bid[o], evalHand(startHand, o, initialContext))
            for (p, excluded, conf) in constraints with p == o:
                if hand[o] & excluded: w *= (1 − conf)
        samples.push((hand[], w))
    normalize w;  ESS = (Σw)² / Σw²
    if ESS < ESS_MIN · K:      // e.g. ESS_MIN = 0.25: the weights are too peaked to be trustworthy
        relax (sigma ×2, every conf ×0.5) and redraw once; if still low, use uniform weights
    return samples

bidLik(o, b, E):
    generic (human):        exp(−(b − E − bias[o])² / (2·sigma[o]²))          // sigma default 1.2
    known deterministic AI: (theirBidFunction(startHand, trumpCard, seat, bidsBefore) == b) ? 1 : 0.02
```

`initialContext` is the round's starting state from `o`'s point of view (`live` = full deck minus
the turn-up, `handSize` = R for everyone), computed once per round.

The capacity-proportional pick approximates uniform dealing under the void constraints well
enough; exact uniformity is not needed because the weights dominate anyway.

Cost: O(|U|) per sample plus one `evalHand` per opponent → ~3–5 µs. Draw the samples once per
decision and reuse them for every candidate (see §5.2).

---

## 4. Base policy: the rule-based target hitter

Inputs: my hand `H`, the current trick (lead suit `L`, cards so far, best card `B` and its
holder), my target `T` (my bid, or a candidate bid during bidding rollouts), `won`, and public
info (`live`, `void[]`, `handSize[]`). It uses **only** public info plus its own hand, also when
running inside a simulation — no peeking at sampled hands. That keeps its "sure" judgments
honest and makes simulated opponents behave like real ones.

### 4.1 Legality, mode, intent

```
legalCards(H, L, trump):
    if L == NONE:                      return H
    if H & SUIT[L]:                    return H & SUIT[L]
    if trump != NONE and H & SUIT[trump]: return H & SUIT[trump]     // must-trump
    return H

need = T − won;  rem = tricks remaining including the current one
mode = need <= 0 ? DUCK : need >= rem ? TAKE : BALANCE
```

Note the two "busted" cases collapse into the same modes: if you are already over, every extra
trick costs one more point, so keep ducking; if you cannot reach your bid any more, every trick
you take recovers one point, so take everything. Sabotage (§5.3) is layered on top.

Intent for *this trick*: DUCK → WANT_LOSE, TAKE → WANT_WIN, BALANCE → `balanceDecide()` (§4.6).

### 4.2 The sure-win test

`safeFromLaterPlayers(c)` — c currently beats `B`; can anyone still to play beat it?

```
for each player q yet to play in this trick:
    if trump != NONE and suit(c) != trump:
        if (unseen & SUIT[trump]) != 0 and not void[q][trump]:        return false   // q might ruff
        if (unseen & ABOVE[c]) != 0 and not void[q][L]:                 return false   // q might overtake in suit
    else:                                                               // c is a trump (or no trump suit exists)
        if (unseen & ABOVE[c]) != 0 and not void[q][suit(c)]:          return false
return true
```

`unseen = live & ~myHand`. This is deliberately conservative: it only says "sure" when the
public information proves it. The rollouts supply the probabilistic version.

### 4.3 Following

```
W = { c in legal : beats(c, B, L, trump) }        // would be winning right now
Lo = legal \ W                                    // cannot win this trick no matter what
sureW = { c in W : safeFromLaterPlayers(c) }

WANT_LOSE:
    if Lo not empty: return shed(Lo)              // dump the loser that is most dangerous to keep (§4.5)
    return highest(W)                             // forced to win: get rid of the biggest card

WANT_WIN:
    if sureW not empty: return lowest(sureW)      // cheapest certain win
    if W not empty:
        if amLastToPlay or not winnersScarce: return lowest(W)
        return highest(W)                         // maximise P(win now) when I cannot afford to lose winnable tricks
    return keep(Lo)                               // cannot win: throw the least valuable card (§4.5)

winnersScarce := evalHand(H) <= need + 0.5
```

### 4.4 Leading

For every card, estimate `pLed(c)` = P(c wins if led), from public info:

```
hi = popcount(unseen & ABOVE[c]);  pTop = (hi == 0) ? 1 : (1 − q·CATCH)^hi
pRuff as in evalHand, except an opponent known void in suit(c) and not known void in trump
      counts with pVoid = 1
pLed(c) = pTop · (1 − pRuff)                     (for trumps and in no-trump rounds: pLed = pTop)
certain(c) = hi == 0 and (trump == NONE or suit(c) == trump or popcount(unseen & SUIT[trump]) == 0
                          or every opponent is known void in trump)

WANT_LOSE:
    choose argmin pLed(c); tie-breaks in order:
      non-trump over trump;
      the suit with the most live cards outside my hand (opponents must follow, someone must beat me);
      if I hold trumps: prefer my longest suit (do not create voids — voids force me to ruff);
      else: prefer my shortest suit (voids give me free discards later);
      lowest rank

WANT_WIN:
    C = { c : certain(c) }
    if C not empty:
        if trump != NONE and (C & SUIT[trump]) != 0 and popcount(unseen & SUIT[trump]) > 0 and need >= 2:
            return highest(C & SUIT[trump])        // draw trumps while I control them, so side winners survive
        return the c in C whose suit has the most live cards outside (least ruff exposure); lowest rank there
    return argmax pLed(c); tie → higher rank
```

Leading your *lowest* card of a suit in DUCK mode is not arbitrary: nobody can duck under it, so it
wins only if every opponent is void in the suit and out of trumps — which the Tracker can see
coming (`popcount(unseen & SUIT[s]) == 0`) and the tie-breaks avoid.

### 4.5 Discards and "which loser to throw"

```
danger(c) = 1 − popcount(unseen & ABOVE[c]) / (popcount(unseen & SUIT[suit(c)]) + 1)
            // ~ probability this card wins a trick if I am ever forced to play it

shed(Lo):   // WANT_LOSE
    if following suit, or under-trumping: return highest(Lo)
    if discarding (void in L and no trump held, or no trump suit):
        if I hold trumps (DUCK mode with trumps is dangerous): discard highest card of my longest non-trump suit
        else: discard argmax danger(c); tie → shorter suit (create a void for free discards)

keep(Lo):   // WANT_WIN but this trick is lost
    if following suit: return lowest(Lo)
    if discarding: lowest danger among non-trumps; never discard a trump while a non-trump exists;
                   if I hold trumps, prefer shortening a suit (enables ruffs)
```

### 4.6 BALANCE: the alignment rule (the real "hit it exactly" decision)

When you need some but not all of the remaining tricks, the question is *which* tricks to take.
Rule: take or lose this trick so that what you still need afterwards is as close as possible to
what your remaining cards will naturally produce.

```
balanceDecide():
    cW = the card §4.3/§4.4 would play under WANT_WIN;  cL = under WANT_LOSE
    if following: if W empty → WANT_LOSE;  if Lo empty → WANT_WIN
    pW = following ? (cW in sureW ? 1.0 : pHoldsUp(cW)) : pLed(cW)
         // pHoldsUp: the pLed formula of §4.4 restricted to the players still to play in this trick
    pL = following ? 1.0 : 1 − pLed(cL)
    E_W = evalHand(H \ cW);  E_L = evalHand(H \ cL)          // expected tricks from the rest of the hand
    scoreW = pW·(−|(need − 1) − E_W|) + (1 − pW)·(−|need − E_W|)
    scoreL = pL·(−|need − E_L|)       + (1 − pL)·(−|(need − 1) − E_L|)
    if cW is a non-trump card with hi == 0 and popcount(unseen & SUIT[suit(cW)]) <= number of opponents:
        scoreW += WASTE_BONUS          // a top card in a suit that is running out: use it or lose it to a ruff (≈ 0.4)
    return scoreW >= scoreL ? WANT_WIN : WANT_LOSE
```

This is a genuine 1-ply lookahead with a state evaluation, which LowRisk lacks entirely: it will
duck with an ace in hand when it needs only one more trick and the ace is still safe, and it will
cash that ace early when the suit is about to be ruffable.

---

## 5. Card-play decision (Monte-Carlo layer)

### 5.1 Candidate reduction

```
choosePlay():
    legal = legalCards(myHand, L, trump);  if |legal| == 1: return it
    cand = one representative per equivalence class:
           c1 ~ c2  iff same suit and (unseen & BETWEEN(c1, c2)) == 0    // no live card between them
    if following:                                                       // W / Lo as in §4.3
        if mode == DUCK and (cand ∩ Lo) not empty: cand = cand ∩ Lo     // winning is dominated: costs ≥ 1 point
        if mode == TAKE and (cand ∩ W)  not empty: cand = cand ∩ W
    if |cand| == 1: return it
    if rem <= ENDGAME_N: return endgameSolve(cand)                     // optional, §5.5
    S = drawSamples(K_play)
    base = representative in cand of the class containing basePolicy(me)'s card
    for c in cand: val[c] = Σ_s w_s · rollout(s, c) / Σ_s w_s        // SAME samples for every c
    pick = argmax val
    if val[pick] − val[base] < MARGIN: pick = base                     // §5.4
    return pick
```

Equivalence classes matter: on the first trick of an 8-card hand you often have 8 legal cards
but only 3–4 distinct decisions.

### 5.2 Rollouts

```
rollout(sample s, first card c):
    state = copy of: my hand, hands from s, current trick so far, won[], live, void[]
    play c for me; finish the trick: each remaining player q plays basePolicy(q, hand_q, target = bid[q])
                                     using public info of `state` plus hand_q only
    resolve winner; winner leads; repeat until the round ends
    return utility(myTricks, tricks[])
```

Use the same K samples for every candidate (common random numbers): the difference between two
candidates is then a paired comparison, which cuts the variance by an order of magnitude compared
to sampling separately per candidate. Seed the RNG per decision so any move can be replayed
exactly when you debug.

Inside rollouts the simulated players use `bid[q]` as targets. A simulated opponent who has already
missed will duck or grab per §4.1, exactly like a real one.

### 5.3 Utility: own score, streak, sabotage

```
utility(t, tricks[]):
    own = (t == myBid) ? 5 + myBid : −|myBid − t|
    if R > 1:                                            // streak rule ignores 1-trick rounds
        own += (t == myBid) ? streakBonusIfHit : −streakPenaltyIfMiss
        // both supplied by the engine's streak logic for THIS round (10 when this round would
        // complete a run of 5, otherwise 0 — or whatever your exact rule pays)
    sab = 0
    for each opponent o: sab += lambda[o] · ((tricks[o] == bid[o]) ? 5 + bid[o] : −|bid[o] − tricks[o]|)
    return own − sab
```

`lambda[o]` = 0.25 by default; 0.5 for the current score leader (and/or the human seat). Because
a hit is worth 5+ points to you and the sabotage term is scaled down, the AI never sacrifices its
own bid to hurt someone — but whenever its own outcome is locked in (all remaining cards are sure
losers, or sure winners) or already busted, the sabotage term is all that is left and the rollouts
start actively wrecking other people's bids. Three things it discovers on its own:

- **Forced ruff**: an opponent in DUCK mode who is known void in a suit and likely holding a
  trump *must* trump when that suit is led. Leading it busts them. This is the single nastiest
  weapon the must-trump rule hands you, and the Tracker knows exactly when it applies.
- **Feeding**: giving extra tricks to a player who is already over costs them a point each.
- **Denying**: overtaking a TAKE-mode player cheaply when your own bid is safe.

Because bids never sum to the trick count, at least one player must fail every round; this term
is how you make sure it is not you.

### 5.4 Regularization

`MARGIN` (≈ 0.15 points with K = 200) stops rollout noise from flipping a decision away from the
base policy's choice when the candidates are near-equal. It also guarantees the MC layer is never
worse than the base policy in a measurable way — useful when you tune.

### 5.5 Optional endgame exact search (Brutal difficulty only)

With `rem <= ENDGAME_N` tricks left, replace rollouts by an exact max-n search per sample: at
each node the player to move picks the card maximizing their own `utility` (their score, minus
their sabotage term); return my utility at the root; average over samples. Branching is tiny
(each player holds `rem` cards), so for N ≤ 4 use `ENDGAME_N = 3` ((3!)^4 ≈ 1.3k leaves per
sample), for N ≥ 5 use `ENDGAME_N = 2`. Caveat: max-n assumes opponents see the deal, which is
optimistic about their precision; keep it to the last 2–3 tricks where humans really are that
precise. Hard difficulty and below can skip this entirely — the rollouts already get the endgame
nearly right because the sampler has usually pinned the hands down by then.

### 5.6 Worked example (why a human finds this unpleasant)

4 players (32-card deck, 7–A), 5-trick round, trump ♠. Bids: P1 = 2, P2 = 0, P3 = 1,
Reckoner = 1. In trick 2 someone ruffed with ♠J, and P2, unable to follow ♥, played ♠7 under it.
Tracker: P2 is void in ♥ and has shown a trump; the rank-gap rule (P2 in DUCK mode undertrumped
with its highest losing trump) adds "P2 holds no ♠8–♠10". P2 bid 0, so the bid-plausibility
weight favours samples where P2 has no *other* trump, but a decent share of samples still give
P2 a ♠Q/K/A. After trick 3 Reckoner has its one trick (DUCK mode) and leads holding ♥7 and ♣J.
♥7 is a near-certain loser (nobody can duck under the lowest heart; it wins only if everyone is
void in ♥ and out of trumps, which the Tracker checks against the live-card masks), and in every sample where P2 still
holds a trump, P2 is *forced* to ruff and wins the trick unless someone overtrumps — busting a
bid-0 player. The sabotage term picks ♥7 by a wide margin. ♣J is the human-looking "safe" lead,
and it risks winning a trick outright if clubs are nearly exhausted. LowRisk would simply lead
its weakest card by rank and never notice either effect.

---

## 6. Bidding

The bid is chosen by asking, for every legal value b, "if I commit to b and then play toward it,
how often do I hit it, and what does that score?" — over sampled deals, with the opponents
playing toward their own bids (declared ones, or guessed ones for players who bid after me).

### 6.1 General rounds (R ≥ 2)

```
chooseBid(R, hand, trumpCard, bidsSoFar, forbidden):
    initialize Tracker for the round (nothing played; live = deck minus turn-up)
    S = drawSamples(K_bid)                       // weights use only the bids already declared
    for s in S:
        for each opponent o with bid[o] == UNBID, in seat order:
            tgt[o] = clamp(round(evalHand(startHand_o) + bias[o]), 0, R)
            if o is the last bidder and the bids would sum to R: move tgt[o] one step (up if the
                fractional part of the estimate was >= 0.5, else down; clamp to [0, R]).
                This depends on my own candidate b, so evaluate the last bidder's target per b.
        for b in 0..R:
            t_s[b] = tricks I take in rollout(s) from trick 1, opener leading, me playing the base
                     policy with target b, opponents with targets tgt[]
    for b in 0..R with b != forbidden:
        U[b] = Σ_s w_s · ( score(t_s[b], b) + streakTerm(t_s[b] == b) ) / Σ_s w_s
               where score(t, b) = (t == b) ? 5 + b : −|b − t|
    return argmax U[b]; ties → lower b
```

What this captures that LowRisk cannot:

- **Table pressure.** If the players before me bid high (sum near or above R), the simulated
  opponents fight for tricks, my rollouts take fewer, and U peaks lower — the classic "everyone
  wants tricks, so bid low and let them collide". If they bid low, tricks get forced onto
  someone and U peaks higher. No hand-crafted adjustment needed; it falls out of the rollouts.
- **Trump length and control.** A long trump suit draws trumps in the rollout and then cashes
  side winners; a short one gets over-ruffed. The base policy plays both correctly, so the hit
  curve reflects it.
- **Steerability, not just strength.** A hand of aces and deuces hits many targets; a hand of
  all middle cards hits none reliably. U[b] measures P(hit b) *given that I steer toward b*,
  which is what the scoring rule pays for.
- **Voids from round shape.** In short rounds the sampler deals opponents 2–3 cards each, so
  voids and ruffs dominate the rollouts and aces are discounted automatically.

The rollouts use the base policy for me, which is weaker than the MC player that will actually
play the hand, so U is slightly conservative. That bias is in the safe direction (it prefers
bids with high simulated hit rate). If tournament results show under-bidding, sharpen with
`P(hit)^GAMMA`, GAMMA ≈ 0.85 (§9).

### 6.2 One-trick rounds

Play is forced, so the only question is P(my card wins). Either compute it exactly (enumerate
the leader's card if I do not lead, then `hyper0` for the remaining opponents) or just run the
generic sampler with K = 2000 — rollouts are one card each. Then:

```
EU(bid 1) = 7p − 1,   EU(bid 0) = 5 − 6p     →   bid 1  iff  p > 6/13 ≈ 0.46
```

subject to `forbidden`. The last bidder in a 1-trick round is always fully forced, and the
streak rule ignores these rounds, so nothing more is needed.

### 6.3 The last-bidder restriction

LowRisk steps down by one when its ideal is barred. Reckoner simply removes the barred value from
the candidate set and takes the best of the rest, which picks up or down depending on whether the
hand is easier to *push* (extra trumps, long suit, ruff potential) or to *shed* (few unavoidable
winners). Frequently the barred value is not even the mode, in which case nothing changes.

### 6.4 Standings-aware risk (optional)

Late in a game, if far behind, multiply the hit bonus for higher bids by (1 + RISK) so U prefers
higher-variance, higher-payoff bids; if far ahead, set RISK < 0. Default RISK = 0.

---

## 7. Opponent modeling — what is inferred from what

| Evidence | Effect | Hard/soft |
|---|---|---|
| Fails to follow suit | void in that suit | hard |
| Discards a non-trump when unable to follow | void in trump too (must-trump rule) | hard |
| Declared bid vs. Evaluator's estimate of a sampled hand | sample weight (Gaussian, per-opponent bias/σ learned from reconstructed hands, §1.5) | soft |
| Declared bid of a deterministic AI (LowRisk, Ducking) | invert its bidding function: samples where it would not have bid that get weight 0.02 | nearly hard |
| Which loser/winner was played, given the player's mode | rank-gap constraints (§1.4) | soft, conf 0.35–0.98 |
| Bids and tricks of everyone | targets for simulated opponents, so rollouts predict who fights and who ducks | model |
| Score standings | sabotage weights λ (leader/human) | policy |

Note that the more predictable an opponent is, the more the Sampler knows. LowRisk is fully
deterministic in both bidding and play, so against a table of LowRisk players the Sampler ends up
with near-exact knowledge of their hands after two or three tricks, and the rollouts become
close to perfect-information play. Against humans the inferences are softer but still large: the
void inferences alone typically fix a third of the unseen cards' locations by mid-round.

---

## 8. Why each piece beats LowRisk specifically

| LowRisk weakness | Reckoner mechanism |
|---|---|
| Static ace/trump count, wrong in short rounds and no-trump rounds | Evaluator's `pRuff`/`q` (§2) and, above all, bidding by rollouts (§6): bids are calibrated to *this* round's hand sizes, trump situation and the sampler's dead pile |
| Ignores opponents' bids | Bids shape sample weights (§3) and set simulated opponents' targets (§6.1) → table pressure is priced in; LowRisk overbids into crowded rounds and busts |
| Step-down-by-one when barred | Best legal alternative by simulated hit rate (§6.3) |
| No memory of played cards | Bitmask Tracker (§1): sure winners/losers become exact; suit exhaustion and trump counts are known; the turn-up card is treated as dead |
| "Win until met, then duck" | Modes + BALANCE alignment rule (§4.6) decide *which* tricks to take; rollouts (§5) do so with lookahead; it will hold an ace and duck, or cash early before a ruff |
| Dumb lead heuristic | Leads chosen by `pLed` from live cards and known voids, with trump-drawing and ruff-exposure logic (§4.4); MC picks among them by outcome |
| No opponent modeling | Voids, must-trump voids, rank-gap constraints, bid inversion, bias calibration (§7) |
| Never tries to make others fail | Sabotage term (§5.3): forced ruffs, feeding, denying — and someone *must* fail every round |
| Fully predictable | Reckoner reads LowRisk's plays as near-certain evidence (conf 0.98) and its bids as an exact function of its hand |

Expected outcome of a 4-seat table with one Reckoner and three LowRisk: Reckoner's hit rate should
be substantially higher and its average round score well above theirs; the gap should widen in
7- and 8-trick rounds where memory and inference matter most, and in 2–3-trick rounds where
LowRisk's ace-counting overbids. If the gap is small in the long rounds, the Sampler or the rank-gap
constraints are not wired correctly.

---

## 9. Knobs and difficulty presets

| Knob | Meaning | Easy | Medium | Hard | Brutal |
|---|---|---|---|---|---|
| `K_play` | samples per play decision (0 = base policy only) | 0 | 60 | 200 | 400 |
| `K_bid` | samples per bid (0 = bid `round(evalHand)` with the ±1 restriction fix) | 0 | 100 | 250 | 500 |
| memory | Tracker scope | current trick only | full | full | full |
| bid weighting `sigma` | width of bid plausibility (off = uniform) | off | 1.5 | 1.2 + calibration | 1.0 + calibration |
| rank-gap `conf` scale | multiplier on §1.4 confidences | 0 | 0.5 | 1.0 | 1.0 |
| `lambda` | sabotage weight (default / leader) | 0 | 0.15 / 0.15 | 0.25 / 0.5 | 0.4 / 0.6 |
| `MARGIN` | stickiness to base-policy move | – | 0.3 | 0.15 | 0.1 |
| `ENDGAME_N` | exact max-n depth | 0 | 0 | 0 | 2–3 |
| `epsilon` | random legal card with this probability (dumbing-down) | 0.15 | 0.05 | 0 | 0 |
| bid noise | add ±1 to the bid with this probability | 0.2 | 0.05 | 0 | 0 |
| `GAMMA` | P(hit) sharpening in bidding | 1 | 1 | 0.9 | 0.85 |

Evaluator / base-policy constants, same at every level: `CATCH` 0.7, `RUFF_W` 0.4, `LONG_W` 0.5,
`WASTE_BONUS` 0.4, `ESS_MIN` 0.25. Tune them by tournament, not by hand.

Easy is roughly LowRisk-with-memory; Medium already beats LowRisk clearly; Hard is the target
"skilled human" setting; Brutal adds the endgame solver and heavier sampling.

Tuning method: round-robin tournaments of ≥ 5,000 rounds per configuration, fixed RNG seeds,
coordinate-descent on one knob at a time, metric = mean points per round at a mixed table
(1 Reckoner + 3 LowRisk, then 2 + 2, then Reckoner vs Reckoner for self-consistency).

---

## 10. Computational cost and engineering notes

Single-threaded, modern x86, C++ with bitmasks (all estimates; measure):

| Operation | Cost |
|---|---|
| Tracker update | < 0.1 µs |
| `evalHand` (≤ 8 cards, ≤ 5 opponents, hypergeometric via 8-step products or a lookup table) | 0.1–0.3 µs |
| Base-policy decision (incl. two `evalHand` calls in BALANCE) | 0.3–1 µs |
| Sample (constrained deal + weights) | 3–5 µs |
| Rollout (≤ 8 tricks × ≤ 6 players, hands shrinking) | 10–40 µs |
| **Play decision**, worst case (trick 1 of an 8-card round, 6 players, 8 distinct candidates, K = 200 → 1,600 rollouts) | 20–60 ms |
| **Play decision**, typical (later tricks, 2–4 candidates after equivalence/domination pruning) | 1–10 ms |
| **Bid**, K = 250 × 9 targets = 2,250 rollouts | 30–80 ms, once per round |
| Endgame max-n, K = 100, N ≤ 4, `ENDGAME_N` = 3 | 10–50 ms |

Samples are independent, so `K` splits across cores trivially (std::thread / OpenMP); with 4
cores every decision lands under ~15 ms. If you want a hard real-time budget, make `K` adaptive:
run in batches of 50 samples until either the leader's margin over the runner-up exceeds
2·(standard error) or the time budget is spent.

Implementation notes:

- Precompute `hyper0` as a table `[n ≤ 48][k ≤ 16][h ≤ 8]` of floats; that removes the only
  division loops from the hot path.
- Keep one `RoundState` struct that is trivially copyable (a few masks and small arrays) so a
  rollout is `copy; loop; return`. No heap allocation inside rollouts.
- Determinism: seed the RNG from (game seed, round, trick, seat) so a decision can be replayed
  when you debug a bad play.
- Sanity assertions that catch rules bugs early: the sampler never needs the void-relaxation
  fallback; the sum of sampled hand sizes plus dead equals `|unseen|`; every rollout ends with
  `Σ tricks == R`.
- Ablation harness: switch off memory, bid weighting, rank-gap constraints, sabotage, and the
  alignment rule one at a time and check each costs points. That tells you which parts are
  actually doing the work at your table sizes and is the fastest way to find a mis-wired
  inference.
- Strategy-fusion check: if the AI starts making "hero" plays that only work when it knows the
  deal, lower `K_play`'s reliance by raising `MARGIN`, or make sure the base policy inside
  rollouts is really information-limited (no peeking at sampled hands).
