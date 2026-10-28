# CLAUDE.md

## Comments

- A comment says why the code is the way it is, when the code cannot say it. One to three lines.
- Never mention phases, plan sections, PR numbers, review findings, dates or who decided something.
  That is history, and history goes in the commit message.
- Never describe code that was removed or what the code used to do.
- No capitals for emphasis. Keep the warnings that prevent a crash, a deadlock or a hang, and make
  them short.
- Say a reason once, next to the code it explains. Don't send the reader to another function.

This applies to every comment you write or change. Existing comments that break it are cleaned up
in PRs of their own, one area at a time, not mixed into unrelated changes.
