# Public GitHub Pages Showcase — Design

Date: 2026-09-21  
Status: approved for planning (pending owner review of this written spec)  
Audience: employers, recruiters, hiring managers, and engineers

## Goal

Replace the internal Field Notes site with a public-facing GitHub Pages showcase that sells **Josh Band + Aetherfield** together: full-stack ownership of a hard audio system (portable DSP → AUv3 path → decisions/docs → verification), with **agentic coding / AI fluency** as a visible through-line and one focused Home section.

## Non-goals (v1)

- CMS, blog, or marketing funnel tooling
- App Store assets or fake shipped plugin UI presented as real
- Agent-role mythology (Astra/Sol/Terra/Luna) as primary brand or navigation
- Implying finished product sound, commercial host readiness, or UI completeness

## Decisions locked

| Topic | Choice |
|---|---|
| Primary audiences | Hiring skim + engineering drill-down |
| What to sell | Full-stack ownership |
| Relation to Field Notes | Replace; absorb useful proof; drop ops chrome |
| Proof set | Audio + diagrams + process proof |
| Branding | Equal dual brand (builder + product) |
| IA | Two pages: Home + Engineering |
| Stack | Hand-authored HTML/CSS/JS; no framework |
| Hosting | GitHub Pages from `/docs` |
| AI fluency | Through-line + dedicated Home section |

## Narrative spine

1. Dual-brand frame: Josh Band + Aetherfield
2. Product thesis: ambient/textural reverb as a measured portable foundation toward AUv3
3. Ownership chain: decide → plan → implement → independently verify → record
4. Agentic practice: human-gated, evidence-driven multi-agent engineering
5. Proof: listen, architecture, measurements/tests, ADR discipline
6. Honest boundary: shipped vs deferred
7. Close: contact + repo + Engineering deep dive

## Site map and hosting

Public URL target: `https://joshband.github.io/aetherfield/`

GitHub Pages source: `/docs`

| Path | Role |
|---|---|
| `docs/index.html` | Home (hiring skim) |
| `docs/engineering.html` | Engineering case study |
| `docs/assets/` | Shared CSS/JS, diagrams, optional canvas helpers |
| `docs/audio/` | Curated short listening WAV/MP3 clips with captions |
| Existing `docs/*.md`, `docs/decisions/`, etc. | Authoritative engineering records (linked, not duplicated) |

Retire `docs/site/index.html` after content absorption. Update README and `docs/start-here.md` to point at the public site URL and note Field Notes retirement.

## Home page sections

1. **Hero** — dual brand as hero-level signal; one thesis; one supporting sentence that includes agentic ownership; CTAs: Listen · Engineering · GitHub. Full-bleed atmospheric plane; no cards/badges in hero.
2. **What this proves** — three ownership claims: portable DSP rigor, host-bound AUv3 path, evidence discipline (AI-fluent ownership named here).
3. **Agentic engineering practice** — focused section: written decisions → interface plans → implementation → independent re-verification from disk; owner as gate; models as role-matched labor. Concrete, not tool logos.
4. **Listen** — 2–4 short labeled clips; captions state engineering/listening evidence, not finished product polish.
5. **System at a glance** — one signal-path diagram of what exists now.
6. **Boundary** — shipped vs deferred (UI, Freeze/Bloom/Texture, full HT matrix, commercial hosts).
7. **About / contact** — short builder blurb; GitHub; LinkedIn/email placeholders until supplied.

## Engineering page sections

1. Problem / thesis
2. Architecture (FDN → diffusion/stereo → wrapper bridge → AUv3)
3. Hard parts (realtime safety, parameter bridging, hybrid bypass, measurement contracts)
4. Proof table (suites, DS checks, listening rounds) with links into repo docs
5. Decision discipline (selected ADR highlights)
6. Agentic method as delivery system (woven into proof narrative; not cosplay)
7. What’s next / open acceptance work

## Visual system

- Direction: “instrument craft” — atmospheric spatial field, not SaaS dashboard, not Field Notes ops panel
- Palette: deep cool field + soft spatial gradient/grain; warm copper/brass accent; cool teal for links/proof
- Type: distinctive display for brand; clean body; mono only for metrics/code
- Motion: 2–3 intentional cues (hero atmosphere, listen focus, section reveal)
- Avoid: purple glow defaults, cream+terracotta cliché, broadsheet density, floating promo chips

## Honesty and evidence rules

- Listening clips labeled as engineering/listening evidence
- Numbers and suite counts must match current `docs/testing.md` / `docs/start-here.md` at publish time
- Separate factual shipped proof from illustrative diagrams
- No claim of App Store readiness or finished UI

## Content sources to absorb from Field Notes

Keep/adapt: decay visualization idea, signal-path diagram, honest status phrasing, measurement narrative.  
Drop: sticky ops sidebar, agent roster as product, stale stat tiles, multi-page SPA chrome.

## Audio production note

No listening media currently lives in the repo. v1 requires exporting a small curated set from existing offline renderers (`render_diffusion_stereo` / listening batch tools) into `docs/audio/` with captions grounded in recorded listening/measurement context.

## Success criteria

- Recruiter can explain what you built and why it matters in under two minutes
- Engineer can drill into architecture and proof without leaving for a scavenger hunt
- AI fluency is obvious without reading as “prompt-only” work
- Status boundaries remain credible against on-disk records
