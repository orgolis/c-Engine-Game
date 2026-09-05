# Licensing — rationale and open items

Companion to [`../LICENSE`](../LICENSE). The licence file is the legal document; this
is the reasoning behind it and the list of things still to settle.

---

## ⚠ This has not been reviewed by a lawyer

The licence was drafted to match a stated intent, not by a solicitor, and a royalty
obligation is a **contract term** — it only means anything if it is enforceable where
you live and where your users do. Before relying on it:

- **Have it reviewed.** A few hundred currency units of an IP lawyer's time is cheap
  against a clause that turns out unenforceable the first time it matters.
- **Settle the governing-law clause.** Section 11 currently says "the Licensor's place
  of residence", which is vague on purpose because the jurisdiction was not known when
  it was written. Name the country.
- **Decide the copyright holder.** It currently reads `orgolis <orgolis123@gmail.com>`,
  a handle and an email. A legal name, or a company once one exists, is stronger. The
  licence already allows assignment to a future entity (section 9(c)) precisely so this
  can be changed later without re-licensing.

Until reviewed, treat the terms as a clear statement of intent rather than a
battle-tested instrument.

---

## Why not an open-source licence

The requirement was: public use, credit to the original developer, and a share of
revenue from commercial games built with the engine.

**No OSI-approved licence can do the third one.** The Open Source Definition forbids
restricting commercial use or any field of endeavour, so a royalty is categorically
outside it. Calling this "open source" would be inaccurate, and the community reacts
badly to that specific inaccuracy.

What it *is*, is the **Unreal Engine model**: source-available, free to learn and
build with, royalty above a revenue threshold. That is a well-understood and
respected arrangement.

Alternatives considered and rejected:

| Option | Why not |
|---|---|
| MIT / Apache-2.0 | Cannot require a royalty at all |
| Business Source License 1.1 | Restricts production use for N years then converts to open source — no royalty mechanism, and eventual conversion gives up the revenue model |
| PolyForm Noncommercial | Bans commercial use outright rather than sharing in it — worse for adoption *and* for revenue |
| Dual licence (GPL + commercial) | Would work, but GPL for the free tier makes every game built with it GPL, which no game developer will accept |

## Why these specific numbers

- **5% above USD 100,000 lifetime per product** mirrors Unreal's structure closely
  enough to be immediately legible to game developers, who already know that shape.
- The threshold is **per product, lifetime, and permanent** — the first $100k is never
  clawed back. A hobbyist or a small commercial release owes nothing, ever, which
  keeps adoption unobstructed while the engine is unknown.
- **Gross revenue, not profit.** Profit is arguable and auditable only with access to
  someone's books; gross is a number a store dashboard shows.

Both numbers are adjustable — but note that **raising them later only binds new
versions.** Section 9(d) keeps every released version available under the licence it
shipped with, which is the honest arrangement and also the one that stops people
fearing a rug-pull.

## Why the "cannot resell the Engine" clause

Section 5(a) is the one restriction with teeth. Without it, someone can take the
engine, rename it, and sell it as their own — which is the only use that competes
directly rather than building on top. Shipping a *game* that embeds the engine is
explicitly fine; shipping *the engine* is not.

---

## Open items

1. **Legal review** — see above.
2. **Governing law** — name the jurisdiction in section 11.
3. **Copyright holder** — legal name or entity in place of the handle.
4. **Vendored licence texts.** `stb`, `vma`, `glad`, `meshoptimizer`, `pocketpy`,
   `tinygltf` and `glslang` are vendored as bare sources with **no upstream `LICENSE`
   file beside them**. MIT and BSD both require the notice to travel with the code.
   `THIRD_PARTY_NOTICES.md` currently carries that obligation alone, which is thin —
   place each upstream licence file next to its source.
5. **Release archives should include `THIRD_PARTY_NOTICES.md` and `LICENSE`.** They
   currently ship neither, so every binary release is distributing MIT-licensed code
   without its notice. This is the most concrete of these items and the cheapest to
   fix: add both files to the CPack/release packaging.
6. **Attribution enforcement is honour-based** and that is fine, but the engine could
   make compliance the path of least resistance — a built-in "Made with
   GameWorldshaper" splash that a product gets for free unless it opts out.
