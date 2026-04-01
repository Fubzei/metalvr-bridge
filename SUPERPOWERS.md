# MetalVR Bridge — Multi-Agent Workflow

Every coding task follows this pipeline. No exceptions.

## Agent 1: SCOUT (Read & Assess)
- Read the relevant source files before changing anything
- Check docs/MILESTONES.md for current status
- Check docs/PROJECT_STATUS.json for machine-readable state
- Check the wiki for any relevant context
- Run: repomix (if touching >5 files) for full context
- Output: one-paragraph assessment of what needs to change and why

## Agent 2: ARCHITECT (Plan & Spec)
- Write a brief spec: what files change, what functions are added/modified
- Identify risks: duplicate symbols, missing dispatch entries, ABI breaks
- Check for conflicts with existing code patterns
- Output: numbered list of changes with file paths

## Agent 3: BUILDER (Implement)
- Make the changes specified by the Architect
- Follow CLAUDE.md rules strictly
- Minimal diffs — do not rewrite files unnecessarily
- Wire new Vulkan functions into dispatch table
- Output: the actual code changes

## Agent 4: VERIFIER (Check & Validate)
- After every change, run these checks:
  1. No duplicate symbols: grep for function name in vulkan_icd.cpp and .mm files
  2. Dispatch table wired: grep the function name in vulkan_icd.cpp kProcTable
  3. Header matches implementation: compare .h declarations to .mm definitions
  4. No orphaned stubs: check that replaced stubs are actually removed
  5. Host tests pass: run scripts/run_host_checks.ps1 (if on Windows)
  6. Build sanity: check CMakeLists.txt includes new files
- Output: verification report (PASS/FAIL per check)

## Agent 5: DEPLOYER (Commit & Monitor)
- Commit with descriptive message
- Push to GitHub
- Monitor CI at https://github.com/Fubzei/metalvr-bridge/actions
- If CI fails: read logs, loop back to Builder with the specific error
- If CI passes: update docs/MILESTONES.md and docs/PROJECT_STATUS.json
- Output: CI status and next action

## Agent 6: CODEX (Token-Efficient Workhorse)
- Codex is delegated mechanical, repetitive, or bulk tasks to save Claude tokens
- Codex does NOT make architectural decisions — Claude is the lead
- Codex runs via the codex plugin or via ChatGPT Codex connector on GitHub
- After Codex completes work, VERIFIER always checks the output

### DELEGATE TO CODEX (saves tokens):
  - Bulk grep-and-replace across multiple files
  - Adding boilerplate dispatch table entries for new functions
  - Writing repetitive test cases (same pattern, different inputs)
  - Formatting/lint fixes across the codebase
  - Adding logging statements to multiple functions
  - Generating documentation from existing code
  - Wiring KHR aliases into the dispatch table
  - Expanding stub implementations that follow an established pattern
  - CI workflow YAML adjustments
  - Markdown/doc generation and updates

### KEEP ON CLAUDE (needs judgment):
  - Architecture decisions (how to implement a new milestone)
  - Shader translator logic (SPIR-V parsing, MSL emission edge cases)
  - Metal API selection (which encoder, which storage mode, which sync primitive)
  - Debugging runtime crashes (reading crash logs, forming hypotheses)
  - Verifying correctness of Vulkan-to-Metal mappings
  - Reviewing Codex output for subtle bugs
  - Deciding what to work on next (priority judgment)
  - Any change to the deferred encoding replay engine

### DELEGATION FLOW:
  1. Claude (ARCHITECT) specs the task
  2. Claude writes a clear, scoped prompt for Codex
  3. Codex executes the mechanical work
  4. Claude (VERIFIER) reviews Codex output
  5. Claude (DEPLOYER) commits and pushes if verified

## Execution Modes

### MODE: FIX (for bugs and CI failures)
Scout -> Builder -> Verifier -> Deployer (skip Architect for simple fixes)

### MODE: BUILD (for new milestone work)
Scout -> Architect -> Builder -> Verifier -> Deployer (full pipeline)

### MODE: VERIFY (for auditing existing work)
Scout -> Verifier (read-only, no changes, produce report)

### MODE: SWEEP (for multi-file refactors)
Scout -> Architect -> Builder (file 1) -> Verifier -> Builder (file 2) -> Verifier -> ... -> Deployer

### MODE: RESEARCH (for investigating crashes/runtime issues)
Scout (deep read of logs + source) -> Architect (hypothesis) -> Builder (diagnostic logging or fix) -> Deployer

### MODE: DELEGATE (for token-efficient bulk work via Codex)
Scout -> Architect (write Codex prompt) -> Codex (execute) -> Verifier (Claude reviews) -> Deployer
