# Maintain documentation

Start with the reader's task and use the [site index](../index.md) to select a category. Tutorials provide a runnable path. Guides cover conditions, steps and recovery. Explanations describe tradeoffs. References define checkable contracts. State the applicable version, profile and verification scope.

## Content and translation

Edit the canonical Chinese page first, then review the matching English path. Check that state names, API names, failure semantics, values and units agree. Put field and layout rules in the Wire v1 reference. Put declarations and calling context in public-header Doxygen comments. Keep a single authority for each rule and link to it from navigation, diagrams and other pages.

Take complete examples from maintained runnable programs and tests. Identify omitted initialization, configuration and cleanup in fragments. The site build renders documents without executing Markdown commands. Keep navigation pages for published URLs and validate both paths. Pin historical validation and failed attempts to their original revisions.

## Build and verify

Prepare CMake 3.24 or later, Doxygen and Python. The documentation build reads the checkout and its public headers using the prepared tools. Run from the repository root:

```sh
python -m pip install -r docs/requirements.txt
cmake -S docs -B build/docs
cmake --build build/docs --target xgl_docs
```

After the strict build passes, inspect `build/docs/site/index.html`, both language entry points and the generated API. Keep the C API navigation page `docs/api/index.md` tracked in Git and verify documentation changes from a clean checkout. If the workflow fails, inspect its `documentation-diagnostics` artifact for the logs and tool versions captured before the failure. Record the current build and Pages deployment separately.

Use `mkdocs serve` for a Markdown preview and the complete build for the generated C API. See the [development workflow](workflow.md) for build and test checks.
