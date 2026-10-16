# ISUI — Implementation Rules
Version: 1.0

## 1. Non-negotiable rules
1. Inspect the repository before proposing or changing implementation.
2. Preserve working behavior unless the brief explicitly authorizes a change.
3. Do not replace a framework, router, state system, API layer, authentication flow, or build system just to achieve a visual effect.
4. Never invent endpoints, data, capabilities, or test results.
5. Do not replace real behavior with demo behavior unless explicitly requested.
6. Work incrementally and validate meaningful stages.
7. If a requirement conflicts with the current architecture, report the conflict and recommend a low-risk solution before breaking changes.

## 2. Discovery before editing
Inspect as applicable:
- Repository instructions, README, package scripts, and project conventions.
- Framework, language, build tool, dependencies, and target browsers.
- Routes, entry points, components, styles, design tokens, assets, and fonts.
- API clients, authentication, state, forms, validation, persistence, and error handling.
- Existing tests, build/lint/type-check commands, and known failures.
- Current behavior and baseline screenshots where available.

Summarize verified facts separately from assumptions. For refactors, list critical existing behavior that must remain intact.

## 3. Planning and execution
Unless immediate autonomous execution is requested:
1. Present assessment and plan.
2. Identify likely files to change and risks.
3. Wait for approval before editing.

When approved or explicitly asked to work autonomously:
- Use small, reviewable checkpoints.
- Maintain `IMPLEMENTATION_PROGRESS.md` for multi-stage work.
- Test after each checkpoint.
- If autonomous execution was requested, do not stop after writing a plan.
- Ask only when blocked by a material ambiguity, destructive action, or missing access.
- Do not claim completion while required checks are failing or unrun.

## 4. Architecture and code quality
- Follow established conventions before introducing new patterns.
- Prefer reusable components and design tokens over duplicated styling.
- Separate presentation from business logic where the existing architecture supports it.
- Avoid giant components, unnecessary global state, speculative frameworks, and excessive abstractions.
- Keep dependencies minimal. Justify large dependencies before adding them.
- Add Three.js/WebGL only when real geometry, camera, materials, lighting, or spatial interaction adds value.
- Clean up event listeners, animation frames, observers, subscriptions, and GPU resources.
- Handle loading, empty, error, and offline states where relevant.

## 5. Functional integrity
Preserve applicable route/navigation behavior, API contracts, authentication/authorization, state transitions, persistence, validation, error handling, keyboard/pointer/touch interactions, analytics, feature flags, and deep links. Do not silently rename public interfaces or change data formats. Document any required migration.

## 6. UI implementation
- Use semantic HTML and existing component primitives.
- Reuse tokens for color, typography, spacing, radii, elevation, borders, blur, and motion.
- Implement relevant hover, focus-visible, active, disabled, loading, success, and error states.
- Keep text and controls legible over complex backgrounds.
- Use CSS effects for simple decoration and a 3D renderer for genuine 3D requirements.
- Avoid layout shifts and effects that obscure content.
- Respect reduced motion and touch-device behavior.
- Provide graceful fallbacks if WebGL/canvas is unavailable.

## 7. Security and data handling
- Never expose secrets, API keys, tokens, passwords, or private environment values in client code or logs.
- Do not weaken authentication, authorization, CSP, sanitization, or validation.
- Treat API responses and user input as untrusted.
- Do not send project data to external services unless authorized.
- Avoid unsafe HTML injection and unsafe URL handling.

## 8. Performance
Consider bundle size, code splitting, image/font loading, animation cost, device pixel ratio, pausing hidden animations, mobile fallbacks, GPU resource disposal, WebGL context loss, and excessive blur/DOM layers. Measure when feasible. Never sacrifice essential usability for effects.

## 9. Verification
Run relevant checks available in the repository:
- Build, lint, type check, unit/integration tests.
- Targeted interactions and regression tests.
- Responsive layouts, keyboard navigation, reduced-motion behavior.
- Visual checks at target breakpoints when tools permit.

Do not invent outcomes. Mark checks as passed, failed, not run, or blocked, and explain why. Confirm controls perform their intended actions.

## 10. Completion report
Report:
1. What changed and files changed.
2. Existing behavior preserved.
3. Tests/checks run and actual results.
4. Known limitations, unresolved issues, and assumptions.
5. How to run or preview the result.
6. Recommended follow-up work.

For multi-stage work, update `IMPLEMENTATION_PROGRESS.md` after each checkpoint with completed items, evidence, blockers, and next steps.

## 11. Definition of done
- [ ] Build succeeds, or pre-existing failures are documented.
- [ ] Required functionality remains intact.
- [ ] No fake API or false success state was introduced.
- [ ] Responsive layout and core states are checked.
- [ ] Reduced-motion and fallback behavior are considered.
- [ ] Relevant tests are reported honestly.
- [ ] Changes and limitations are documented.
