# cplot

## CLI for AxiDraw printing

User story: As a user with an AxiDraw device, I want to send Markdown or plain text to be plotted with predictable margins and orientation, and optionally generate a true‑to‑size preview, so that the physical output matches what I planned without trial and error.

---

## Geometry core

User story: As a developer of the plotting/preview subsystems, I need a unit‑agnostic geometry core (Point, Path, Paths) to represent vector strokes consistently so both preview and plotting consume the same shapes.

Requirements:
- Define Point, Path (ordered points), and Paths (ordered paths) with deterministic ordering and bounds queries.
- Unit‑agnostic with canonical conversion (mm, in) without loss beyond defined precision.
- Shared single source of truth for preview and plotting.
- Optional non‑geometric metadata (id, layer, pen, color, tags) must not affect geometry or equality.
- Transform operations: translate, scale (uniform/non‑uniform), rotate about origin or pivot.
- Numeric precision/tolerances suitable for deterministic behavior; reject NaN/Inf; define handling for degenerate paths.
- Provide stable equality and utilities (path/segment length) to support planning and preview metrics.

---

## Canvas

User story: Place geometry into device space (mm) with controlled orientation, margins, and anchoring; provide text frame width for layout; preview and plot use the same placed geometry 1:1.

Requirements:
- Accept input geometry in arbitrary supported units and place into device mm.
- Support orientation (portrait|landscape) and margins (top/right/bottom/left) in mm.
- Provide the text frame width (usable width after margins/orientation) to layout.
- Identity of placed geometry is used by both preview and plot without re‑layout.
- Validate non‑positive drawable area or invalid values with clear errors.
- Deterministic outputs for identical inputs/settings.
- Paper size (mm) is explicit; margins apply to the paper; orientation/anchors relative to paper frame.
- Portrait text frame width semantics: the resulting effective frame width for layout equals the inner height of the paper after margins; landscape uses inner width.

---

## Vector text rendering (Hershey)

User story: Render text into vector strokes using bundled Hershey families so plotted output is device‑friendly and respects requested style with sensible defaults/fallbacks.

Requirements:
- Convert input text into vector strokes via bundled Hershey families.
- Defaults: Sans Regular at 14 pt (or equivalent mm via placement). Bold/italic request maps to closest available variant; record fallback.
- Document and implement fallback policy for missing glyphs while preserving layout; report substitutions.
- Deterministic outputs for identical inputs.
- Expose size compatibly with placement pipeline; 1:1 scaling maintained.
- Support line breaks and spacing rules suited for frames; deterministic line height and alignment.
- Allow selection among bundled families; document available variants.

---

## Text layout behavior

User story: Wrapping within frame width, simple hyphenation; normalize whitespace; preserve spaces in code.

Requirements:
- Wrap words within frame width deterministically.
- Simple hyphenation. When enabled: ASCII‑only naive breaks; prefer existing hyphens; otherwise insert a hyphen at last character that fits with minimum suffix of 3 characters.
- Normalize whitespace outside code spans/blocks; preserve spaces/tabs inside code exactly. Tabs convert to 4 spaces prior to normalization.
- Deterministic layout for identical inputs and settings.

---

## Markdown

User story: Support core Markdown constructs with graceful degradation; render to vector strokes.

Requirements:
- MUST: paragraphs; headings H1–H6; emphasis and strong; inline code and fenced code blocks (preserve whitespace inside code); blockquotes with a left rule and indent (4 mm base, +3 mm per nested level); horizontal rules; bulleted and numbered lists (nesting up to depth 3); simple tables (header row with rule below, per‑column alignment {left, center, right}, min cell padding 2 mm, no merged cells, word wrap within column width, table width ≤ frame width).
- MUST: graceful degradation for unsupported constructs; document behavior; warn when constructs are ignored or degraded.
- MUST: deterministic output for identical inputs/settings.
- SHOULD: expose layout constraints for preview/plot; warn when constructs are ignored or degraded.

Edge cases: deeply nested lists/quotes; wide tables; mixed inline formatting; long code lines/tabs; rule placement at page boundaries.

---

## Font selection and coverage

User story: List bundled Hershey families; choose family and weight; understand glyph coverage/fallbacks.

Requirements:
- Enumerate bundled Hershey families and available weights/styles.
- Allow choosing family and weight/style; fall back to nearest available or default Sans Regular; report fallback.
- Document coverage per family/variant; expose coverage summaries; emit warnings for unsupported glyphs.
- Preserve layout stability under fallback via defined substitution width policy.
- Configurable missing glyph policy (substitute, placeholder, omit); reflect in output metadata.

---

## Device connectivity & manual actions

User story: Find and pick your plotter, test pen up/down and basic movement safely, and see simple device information.

Requirements:
- Detect connected plotters and let the user choose one; confirm the selection.
- Pen controls: up, down, toggle, and a quick test cycle.
- Motors: enable/disable and home to a known reference.
- Move a small amount in X/Y with safety checks to stay within the page area.
- Show basic device info (name and version) and current position when available.
- Clear errors and show recovery status in plain language.
- Keep a simple log of manual actions and results.

---

## Motion planning

User story: Deliver smooth, accurate drawings quickly and reliably, minimizing wasted motion while keeping results faithful to the preview, so users can trust the plotter for finished work without trial and error.

Requirements:
- Smooth, clean motion that preserves line quality; avoid visible wobble or harsh stops.
- Predictable results: printed output matches the preview and repeats consistently with the same inputs.
- Time‑efficient runs by reducing unnecessary travel between strokes while honoring any user‑intended order.
- Safe operation within device and paper bounds; fail gracefully with clear, helpful messages if limits are reached.
- Simple, understandable controls to choose between “faster” and “finer” runs; sensible defaults that work out of the box.
- Consistent behavior across small and large pieces; handles intricate artwork without introducing artifacts.
- Transparent summaries: show estimated vs actual time and note when the system adjusted speed for quality or safety.
- Non‑destructive planning: never alters the original geometry; planning only affects how the machine moves.
- Plain‑language settings and reports suitable for non‑experts; no specialist knowledge required to get great results.

---

## Plotting pipeline

User story: Turn the prepared page into a finished drawing that matches the preview, with smooth pen control, safe bounds, and clear progress from start to finish.

Requirements:
- One source of truth: the same page you preview is the page that gets plotted.
- Smooth execution: lift and lower the pen cleanly and move between strokes efficiently.
- Stay within the page: never draw outside the set paper area; warn and stop safely if limits would be exceeded.
- Reliable delivery to the device so drawings run without hiccups or pauses.
- Clear progress: show percent complete and an estimate of time remaining; allow pause/resume or cancel.
- Consistent output: the same inputs and settings produce the same physical result.
- Helpful recovery: when something goes wrong, stop safely and explain next steps in plain language.

---

## Preview vs Print

User story: See exactly what will be drawn before plotting, and choose whether to fit the content to the page or keep exact size with clear guidance if it won’t fit.

Requirements:
- Separate preview and print modes with identical visuals and layout.
- Preview exports: SVG (always) and optionally PNG at the chosen paper size.
- Exact match: preview and print use the same page and settings; what you see is what you get.
- When content doesn’t fit: by default, stop and explain how to proceed; options include scale to fit (proportional) or clip to the page.
- Be transparent about adjustments: clearly state if scaling or clipping is applied.
- Record the key settings used (orientation, margins, anchors, fonts, layout) for repeatable results.

---

## Configuration & defaults

User story: Set it once and have it “just work” every time, with safe defaults and a simple way to get back to factory settings.

Requirements:
- View current defaults and the factory defaults.
- Choose paper size, orientation, margins, drawing speed, pen up/down positions and speeds, brief pen delays if needed, and automatic power‑off.
- Use the same defaults for preview and print unless explicitly changed for a run; keep settings until changed or reset.
- Reset to factory settings at any time.
- Prevent unsafe or incompatible choices; explain how to correct them.
- Document the factory defaults (paper size, margins, recommended pen positions/speeds, and auto power‑off) so users know what to expect out of the box.
- Prefer clear, human‑readable descriptions for settings and any per‑run overrides.

---

## Errors & reporting

User story: Get clear, helpful messages when something needs attention, and an easy summary of what happened.

Requirements:
- Use simple levels: info, warning, and error. Errors stop the run; warnings do not.
- Write messages in plain language with concrete next steps.
- Summarize unsupported Markdown features and any missing characters in text, with counts and examples when helpful.
- Provide one consolidated summary at the end with categories, counts, and suggested fixes; allow quieter or more detailed output as needed, and an optional JSON report.

---

## Diagnostics & logging

User story: See clear progress, estimates, and simple status while plotting; get enough information to repeat a run or request support.

Requirements:
- Show progress percentage, elapsed time, and estimated time remaining while running.
- Summarize what will be drawn (page size, number of strokes/lines) before starting.
- Log major events: connect/disconnect, start/complete, pause/resume, and any errors, with timestamps.
- Errors include helpful context and recovery hints, and are linked into the end‑of‑run summary.
- Provide an “About”/version command with app version and basic system and device information suitable for support.
- Allow exporting a run summary/log for troubleshooting; avoid collecting sensitive information by default.

---

End of specification.
