# ISUI — Prompt Templates
Adjust file paths to match the repository. These prompts can be used with ChatGPT, Cline, Cursor, or another coding agent.

## 1. New website — plan first
Read `DESIGN_SYSTEM.md`, `IMPLEMENTATION_RULES.md`, and `PROJECT_BRIEF.md`.

Inspect the repository and identify the stack, entry points, conventions, dependencies, and current behavior. Translate the brief into:
1. Concise art direction.
2. Page/route and component structure.
3. Design tokens and reusable components.
4. 3D/motion strategy and fallbacks.
5. Implementation plan and files to create/change.
6. Acceptance and test criteria.

Do not edit files yet. Separate verified facts from assumptions and wait for approval.

## 2. New website — autonomous execution
Read `DESIGN_SYSTEM.md`, `IMPLEMENTATION_RULES.md`, and `PROJECT_BRIEF.md` first.

Inspect the repository and implement the brief end-to-end, including responsive layouts, interaction states, accessibility, and rendering fallbacks. Use real 3D only where it materially improves the result. Do not invent APIs or present a local demo as a real integration.

Work in checkpoints, run relevant checks after each checkpoint, and update `IMPLEMENTATION_PROGRESS.md`. Continue beyond planning. If blocked by material ambiguity, destructive action, missing credentials, or unavailable hardware/service, explain the blocker and ask a focused question. At completion, report changed files, verified behavior, actual test results, limitations, and preview instructions.

## 3. Existing website refactor — plan first
Read `DESIGN_SYSTEM.md` and `IMPLEMENTATION_RULES.md`, then inspect the repository.

Preserve business logic, API contracts, routes, authentication, state management, persistence, analytics, and working user flows. Identify the current architecture and baseline behavior.

Propose a phased refactor with visual direction, components/styles to change, behavior that must remain unchanged, risks, and regression criteria. Do not edit files until I approve. Do not replace the framework or rewrite the application merely to achieve the new style.

## 4. Existing website refactor — autonomous execution
Read `DESIGN_SYSTEM.md` and `IMPLEMENTATION_RULES.md`. Inspect the repository, record critical flows, and establish the available build/test baseline.

Refactor incrementally toward ISUI while preserving working behavior and public contracts. Reuse the current stack where reasonable. Justify major dependencies. Do not substitute fake UI state for real functionality.

After each checkpoint, run relevant tests and update `IMPLEMENTATION_PROGRESS.md`. If a check cannot run, state why. At completion, report changed files, preserved functionality, actual verification results, limitations, and preview instructions.

## 5. Push visual quality further
Read `DESIGN_SYSTEM.md` and the project brief. Improve the experience without compromising usability or maintainability.

Do not merely increase blur, glow, gradients, or animation. Revisit art direction, composition, lighting, material contrast, scale, depth, typography, and the signature visual element. Establish a deliberate foreground/midground/background hierarchy.

Where real 3D is warranted, use suitable geometry, materials, lighting, camera behavior, and interaction. If CSS simulation is used, describe it accurately. Provide responsive and reduced-motion fallbacks. Keep the primary user task immediately understandable.

Explain architectural changes or large dependencies before adding them. Verify target breakpoints and run relevant regression checks.

## 6. Visual prototype only
Read `DESIGN_SYSTEM.md` and `PROJECT_BRIEF.md`.

Build a visual prototype only. Clearly label simulated data and demo-only interactions. Do not connect to a real backend or device unless the brief explicitly requests it. Document what is simulated versus functional and how to run the prototype.

## 7. Review/critique an existing UI
Read `DESIGN_SYSTEM.md` and inspect the current implementation. Review visual hierarchy, distinctiveness, composition/depth, material/lighting/color consistency, usability, responsive behavior, accessibility, performance, fallbacks, and functional integrity.

Rank findings by severity and user impact. Recommend concrete improvements and identify the smallest high-value changes first. Do not edit files unless asked.

## 8. Quick task
Follow `DESIGN_SYSTEM.md` and `IMPLEMENTATION_RULES.md`. The task is: [describe task]. Inspect relevant code and preserve existing behavior. [Plan first and wait for approval / execute autonomously]. Verify the changes and report actual test results and limitations.

## Visual reference requirement

When the repository includes `reference/ISUI_REFERENCE.html`, inspect it and render it in a browser when possible before implementing the UI. Use `reference/ISUI_SPACE_SCENE_REFERENCE.html` only when an atmospheric space/planet treatment is relevant. Learn the visual language; do not copy the demo's application structure or simulated behavior wholesale. Preserve the target project's real functions and adapt the design to its users and information architecture.
