# Implementation reuse

- Inspect existing commands, UI actions, evaluators and shared utilities before implementing a behavior. Extend or reuse the existing system wherever practical.
- Console commands, overlay controls and shortcuts for the same operation must use a shared implementation. Do not introduce parallel pose capture, smoothing, aiming, collision or transition algorithms for different entry points.
- If a separate or replacement subsystem is necessary, explain why the existing system cannot be extended and ask the user before implementing it. Ordinary adapters and helpers that consolidate existing behavior do not require separate approval.
- Keep live and cached evaluation state separate where required for deterministic scrubbing, while sharing the underlying behavior and math.
- Add regression coverage comparing entry points when consolidating behavior; preserve project compatibility and stable internal camera references.
