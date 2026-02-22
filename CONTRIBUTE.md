## Code style guide

To keep the codebase consistent and easy to review, please follow these style rules when you submit a PR:

1. *Formatting*
   - Use the project’s chosen formatter or style settings.
   - Keep indentation, spacing, and line breaks consistent with existing files.
   - Wrap lines at a sensible character length if the project has a limit; otherwise follow the style used in nearby code.

2. *Naming*
   - Give variables, functions, and modules clear, descriptive names.
   - Use the same naming convention already used in the repository (e.g., snake_case or camelCase).

3. *Linting*
   - Run any linter configured for the project before committing.
   - Fix or explain any lint warnings or errors in your PR description.

4. *Comments and documentation*
   - Add comments for non-obvious logic or design decisions.
   - For public functions or modules, include brief docstrings or inline docs if the project uses them.
   - Avoid unnecessary or redundant comments that restate what the code already clearly does.

5. *Dependencies and imports*
   - Import only what is needed.
   - Order imports consistently with existing files (e.g., standard library, third‑party, local modules).
   - If adding a new dependency, ensure it is justified, documented, and approved per project rules.

6. *Testing*
   - When changing logic, add or update tests to cover the new behavior.
   - Follow existing test naming and directory conventions.

If anything here conflicts with an existing section or tool in the repo, follow the repo’s existing style first, then explain any differences in your PR description.