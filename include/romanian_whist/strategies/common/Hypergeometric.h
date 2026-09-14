#ifndef ROMANIAN_WHIST_COMMON_HYPERGEOMETRIC_H
#define ROMANIAN_WHIST_COMMON_HYPERGEOMETRIC_H

namespace romanian_whist::common
{

// P(h cards drawn without replacement from n contain none of k marked cards).
//
// The one question every card-counting strategy keeps asking, in the one form
// that answers it exactly: "are all j of the cards that beat mine somewhere
// other than in the hands still to play?" Mark the beaters, draw the opponents'
// hands, ask for zero hits.
//
// Note the argument order - marked count FIRST, then the population, then the
// draw. It reads backwards from the sentence above and is easy to transpose.
//
// Exact at the edges, which callers rely on: h == 0 is 1.0 whatever n and k
// are, and n - k < h is 0.0 - "there are more cards to deal than there are
// safe ones, so a beater is certainly out". That second case is not a corner:
// it is every 8-trick round, where the whole deck is dealt and nothing is dead,
// and it is what collapses a probability back into a certainty.
//
// Table-backed for n <= 48, k <= 16, h <= 8, which covers every hand-sized
// draw in the game. Larger h - summing several opponents' hands in a big
// round - falls through to the same product computed in double, so it stays
// correct and only gets slower.
float hyper0(unsigned int k, unsigned int n, unsigned int h);

} // namespace romanian_whist::common

#endif
