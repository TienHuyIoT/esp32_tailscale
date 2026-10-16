# ISUI — Design System
Version: 1.0

## Purpose
Reusable visual direction for new websites and UI refactors using **Immersive Spatial UI (ISUI)**: an interface that feels spatial, tactile, layered, and art-directed—not merely a flat page with blur effects.

## 1. Design philosophy
Combine:
- Clear information hierarchy and usability.
- Foreground, midground, and background depth.
- Intentional materials: glass, smoked glass, crystal, obsidian, brushed metal, ceramic, or materials suited to the product.
- Coherent lighting, highlights, shadows, reflections, and restrained glow.
- Motion that communicates state or spatial relationships.
- A distinctive visual world appropriate to the product.

Do not equate ISUI with adding blur everywhere. Effects must support hierarchy and never overwhelm content.

## 2. Art direction
Before implementation, define:
1. World/theme (e.g. deep-space observatory, precision instrument, premium studio).
2. Mood (calm, cinematic, technical, luxurious, playful, industrial).
3. Palette: dominant base, primary accent, limited supporting accents.
4. Material language and lighting direction.
5. Depth model: background, midground, foreground.
6. Motion language and reduced-motion behavior.
7. One signature visual moment.

Avoid arbitrary collections of gradients; establish a coherent direction first.

## 3. Spatial composition
- Use scale, overlap, perspective, contrast, and lighting to create depth.
- Keep panel thickness, edge treatment, shadows, and highlights consistent.
- Use negative space to separate major groups.
- Keep primary actions and essential information easy to find.
- Do not let decorative backgrounds compete with content.
- Keep parallax subtle; simplify it on touch devices and when reduced motion is requested.

## 4. Materials and lighting
- **Glass:** translucent tint, controlled blur, edge highlights, separation from background.
- **Obsidian/dark glass:** deep surfaces, subtle reflections, selective specular accents.
- **Crystal:** sharper edges and restrained prismatic highlights.
- **Metal:** directional highlights and restrained roughness cues.
- **Atmosphere:** haze, particles, distant lights, or volumetric-looking gradients when appropriate.

Pick a dominant light direction. Use shadows for elevation, glow for meaningful focus points, and sufficient contrast for readability. Do not rely on color alone for status.

CSS gradients and shadows can simulate simple depth. Use actual 3D geometry when shape, camera movement, lighting, or spatial interaction materially benefits from it. Describe the implementation accurately: CSS simulation is not a real 3D mesh.

## 5. Color and typography
Define reusable tokens for background, surface levels, text, muted text, borders, accents, success, warning, danger, focus, and disabled states. Use a deliberate type scale and spacing rhythm. Keep body text legible over detailed imagery. Use tabular numerals for telemetry when useful. Avoid long uppercase passages and low-contrast thin text.

## 6. Components and states
Define applicable states for every interactive component:
- Default
- Hover (where applicable)
- Focus-visible
- Pressed/active
- Disabled
- Loading/pending
- Success/error

Use consistent tokens for radii, borders, elevation, blur, spacing, and motion. Controls must perform the actions they claim to perform.

## 7. 3D and immersive elements
Choose the lightest suitable technology:
1. CSS transforms/gradients for simple depth.
2. Canvas/WebGL for procedural visuals or particles.
3. Three.js or an existing 3D engine for real geometry, camera, materials, lights, and interaction.
4. Static image/video when it gives the best quality-to-cost ratio.

For genuine 3D, define camera limits, geometry/materials, lighting/shadows, interaction, loading/failure states, and performance fallbacks.

## 8. Responsive behavior
Compose layouts intentionally for desktop, tablet, and mobile. Do not merely shrink a desktop layout. Prioritize touch target size, readable text, predictable navigation, and simpler effects on small or low-power devices. Never depend on hover for touch devices.

## 9. Motion and accessibility
- Respect `prefers-reduced-motion`.
- Offer low-motion/static alternatives for parallax and camera movement.
- Avoid disorienting motion and flashing effects.
- Maintain keyboard access and visible focus.
- Use semantic HTML and accessible names.
- Check contrast over static and animated backgrounds.
- Manage focus correctly in dialogs.

## 10. Quality bar
A successful ISUI result is distinctive, spatial, usable, readable, responsive, honest about its effects, performant, and maintainable.

## 11. Review checklist
- [ ] Art direction is stated.
- [ ] Color, type, spacing, radii, and elevation tokens are defined.
- [ ] Foreground/midground/background have distinct roles.
- [ ] Effects have purpose and coherent lighting.
- [ ] Primary actions and statuses remain legible.
- [ ] Interaction states are designed.
- [ ] Desktop/mobile layouts are intentionally composed.
- [ ] Reduced-motion behavior and rendering fallbacks exist.
- [ ] CSS, canvas, WebGL, and true 3D are accurately distinguished.

## 12. Visual reference policy

Before designing or refactoring a website, inspect `reference/ISUI_REFERENCE.html` and render it in a browser when possible. Consult `reference/ISUI_SPACE_SCENE_REFERENCE.html` when the project specifically needs an atmospheric space/planet visual.

Use these files as references for spatial composition, glass materials, edge highlights, shadows, lighting, depth, motion, and responsive behavior—not as application code to copy wholesale.

Adapt the visual language to the target product. Preserve the target project's framework, architecture, business logic, APIs, routes, authentication, and working features. Do not copy demo-only behavior into production. Distinguish simulated interactions from real functionality. If an effect cannot be implemented reliably or efficiently, explain the limitation and propose an alternative.

The primary reference is a visual guide, not a mandatory layout. A new product should have its own information architecture and component layout while retaining the chosen ISUI design language.
