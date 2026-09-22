# Public GitHub Pages Showcase Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Field Notes with a dual-audience GitHub Pages site that sells Josh Band + Aetherfield ownership, including visible agentic-engineering fluency.

**Architecture:** Static two-page site published from `/docs` (`index.html` + `engineering.html` + shared `assets/` + curated `audio/`). Absorb Field Notes proof ideas; retire `docs/site/`. Link to authoritative markdown for deep evidence.

**Tech Stack:** Hand-authored HTML5, CSS (custom properties), small vanilla JS; GitHub Pages from `/docs`; optional canvas for decay viz; no framework.

**Spec:** [docs/superpowers/specs/2026-09-21-public-github-pages-showcase-design.md](../specs/2026-09-21-public-github-pages-showcase-design.md)

---

### Task 1: Ground truth status numbers and audio candidates

**Files:**
- Read: `docs/start-here.md`, `docs/testing.md`, `README.md`
- Read: existing Field Notes `docs/site/index.html` (absorb candidates)

- [ ] **Step 1:** Extract current suite counts, DS/listening status, and shipped-vs-deferred boundary into a short publish checklist note (keep in plan handoff / agent-log later).
- [ ] **Step 2:** Identify 2–4 listening renders to export (isolation/corpus clips already described in testing.md); list filenames and captions.
- [ ] **Step 3:** Confirm no secrets or private paths will be published.

### Task 2: Scaffold public site files under `docs/`

**Files:**
- Create: `docs/index.html`
- Create: `docs/engineering.html`
- Create: `docs/assets/site.css`
- Create: `docs/assets/site.js`
- Create: `docs/audio/.gitkeep` (then real WAVs in Task 4)

- [ ] **Step 1:** Add shared CSS variables (atmosphere, type, accents) and base layout (nav, sections, footer) matching the visual system in the spec.
- [ ] **Step 2:** Scaffold Home section shells in order: hero → proves → agentic practice → listen → system → boundary → about.
- [ ] **Step 3:** Scaffold Engineering section shells: thesis → architecture → hard parts → proof → decisions → method → next.
- [ ] **Step 4:** Wire shared nav (Home / Engineering / GitHub) and mobile layout.

### Task 3: Write Home narrative and agentic section

**Files:**
- Modify: `docs/index.html`
- Modify: `docs/assets/site.css` as needed

- [ ] **Step 1:** Write dual-brand hero copy and CTAs; keep hero free of cards/badges.
- [ ] **Step 2:** Write “What this proves” (3 ownership claims including AI-fluent ownership).
- [ ] **Step 3:** Write “Agentic engineering practice” as a concrete method section (decide → plan → implement → independently verify; owner gate).
- [ ] **Step 4:** Write boundary + about/contact with LinkedIn/email placeholders.

### Task 4: Export curated audio and Listen UI

**Files:**
- Create: `docs/audio/*.wav` (or compressed web-friendly formats if size requires)
- Modify: `docs/index.html`, `docs/assets/site.js`

- [ ] **Step 1:** Build Release tools if needed; render curated clips into `docs/audio/`.
- [ ] **Step 2:** Add accessible custom or native audio players with honest captions.
- [ ] **Step 3:** Add one intentional listen-focus motion (CSS/JS), keep subtle.

### Task 5: Diagrams, optional decay viz, Engineering proof page

**Files:**
- Modify: `docs/index.html`, `docs/engineering.html`
- Create: `docs/assets/diagrams.svg` and/or inline SVG
- Optional: `docs/assets/decay.js` (adapted from Field Notes canvas idea)

- [ ] **Step 1:** Add “system at a glance” diagram of implemented path only (dashed deferred).
- [ ] **Step 2:** Fill Engineering page with architecture, hard parts, proof table linked to `testing.md` / ADRs.
- [ ] **Step 3:** Weave agentic method into Engineering proof narrative without role cosplay.
- [ ] **Step 4:** Optionally port a small decay canvas if it remains accurate to current FDN law.

### Task 6: Retire Field Notes and update repo pointers

**Files:**
- Delete or redirect: `docs/site/index.html` (prefer delete after absorption; optional stub redirect if needed)
- Modify: `README.md`, `docs/start-here.md`, `AETHERFIELD_SPEC.md` site reference if present
- Modify: `docs/agent-log.md` with handoff note

- [ ] **Step 1:** Remove Field Notes as the public surface; ensure no broken relative links from README.
- [ ] **Step 2:** Document Pages setup: Settings → Pages → Deploy from branch `/docs`.
- [ ] **Step 3:** Add `.superpowers/` to `.gitignore` if brainstorm companions remain and are untracked noise.

### Task 7: Verify locally and publish checklist

- [ ] **Step 1:** Serve `docs/` locally and check Home + Engineering on desktop and mobile widths.
- [ ] **Step 2:** Verify all internal links, audio playback, and that claimed numbers match testing/start-here.
- [ ] **Step 3:** Confirm honesty labels on audio and deferred work.
- [ ] **Step 4:** Owner enables GitHub Pages; smoke-check `https://joshband.github.io/aetherfield/`.

---

## File responsibility map

| File | Responsibility |
|---|---|
| `docs/index.html` | Hiring narrative, listen, agentic section |
| `docs/engineering.html` | Case study depth and proof links |
| `docs/assets/site.css` | Visual system and layout |
| `docs/assets/site.js` | Nav, motion, audio helpers |
| `docs/audio/` | Curated listening evidence |
| `docs/site/` | Retired after absorption |
