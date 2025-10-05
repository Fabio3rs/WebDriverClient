# Copilot Instructions for WebDriverClient (bidi-x)

## Initial Setup (First Time Working on This Project)

**REQUIRED FIRST STEPS:**
1. Read `map.md` to understand the system architecture and structure
2. Read `CLAUDE.md` for project-specific development guidelines
3. Read other `.md` documentation files in the repository
4. After reading and understanding the project, notify the user and explain your understanding of the project structure

## Critical Rules (MUST FOLLOW)

### 1. Code Verification Protocol (MANDATORY BEFORE ANY CODE CHANGE)

**BEFORE writing ANY line of code, you MUST:**

1. **Identifier Verification**: Use `Grep` tool to check if ALL identifiers (variables, functions, classes, IDs, namespaces) you intend to use already exist in the codebase
   - NEVER declare an identifier without checking first
   - Search pattern: exact name and similar variations

2. **Scope Verification**: If an identifier exists in the same scope/function, REUSE it
   - NEVER redeclare variables in the same scope
   - Understand the existing context before adding new declarations

3. **Functionality Check**: Use `Grep` to search for similar functionality before implementing something new
   - If similar functionality exists: EXTEND or MODIFY the existing code
   - If duplicate functionality exists: CONSOLIDATE into a single implementation

4. **Required Workflow (In Order)**:
   ```
   Step 1: Grep → Verify identifiers and search for similar functionality
   Step 2: Read → Understand the existing code context
   Step 3: Implement → ONLY if functionality does not exist
   Step 4: Build & Test → Always run: cmake --build . -j$(nproc) 2>&1 && ctest --output-on-failure -j$(nproc)
   ```

5. **Single Source Principle**:
   - One functionality = One function/method
   - One identifier = One declaration per scope
   - NEVER duplicate code or identifiers

**WARNING**: Violating these rules will cause syntax errors, compilation failures, and system breakage.

### 2. Naming Conventions (STRICTLY ENFORCED)

**NEVER use emojis or icons in:**
- Log messages
- Commit messages
- Branch names
- File names
- Directory names
- Variable names
- Function names
- Class names
- Method names
- Package/module/namespace names

**ALWAYS use:**
- Clear, descriptive names that explain exactly what the code does
- Self-documenting identifiers
- Consistent naming patterns following C++ conventions (snake_case for variables/functions, PascalCase for classes)

### 3. Documentation and Map Maintenance

**Map.md Update Policy:**
- NEVER update `map.md` without explicit user authorization
- NEVER commit or push without explicit user approval
- Our development process is based on review and approval

**When authorized to update:**
1. Update `map.md` with the latest information about modifications, expansions, additions, or removals
2. Make the commit with a clear, descriptive message (no emojis)
3. Push only after explicit approval

### 4. Development Workflow

**Review and Approval Process:**
- All changes require user review before committing
- Explain what you're about to do before making changes
- Wait for approval before proceeding with significant modifications
- After approval: update documentation, commit, and push (if authorized)

**Build and Test:**
- Always compile and run tests after making changes
- Use the standard command: `cmake --build . -j$(nproc) 2>&1 && ctest --output-on-failure -j$(nproc)`
- Fix any compilation errors or test failures before proceeding

## Code Quality Standards

1. **Function Length**: Keep functions ≤ ~80 lines
2. **Const Correctness**: Apply const where appropriate
3. **RAII**: Use Resource Acquisition Is Initialization pattern
4. **Error Handling**: Use `std::error_code` for normal flow; exceptions only in sync facades
5. **No Escaping Pointers**: No `string_view` or pointer from arena may escape handler scope
6. **[[nodiscard]]**: Mark lazy methods to prevent lost operations

## Project-Specific Guidelines

- **C++ Standard**: C++23 when Boost enabled, otherwise C++20
- **Threading Model**: Single event loop with strand-based synchronization
- **No Busy-Wait**: Zero busy-wait, zero detached threads
- **Lazy Evaluation**: Operations materialize only at terminal (.finally / co_await / sync)
- **Deterministic Timeouts**: Timer racing, late responses ignored

## Summary

**Primary Directive**: Understand before you code. Search before you create. Review before you commit.

This ensures code quality, prevents duplication, and maintains system integrity.
