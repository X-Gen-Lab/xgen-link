# GitHub CI/CD

XGL uses two GitHub automation workflows:

- `CI`: validates code, tests, static analysis, footprint, SDK consumer smoke, no-heap smoke, and documentation.
- `Deploy Docs`: builds the MkDocs + Doxygen documentation site from `main` and deploys it to GitHub Pages.

## CI Triggers

`CI` runs on:

- Pushes to `main`.
- Pull requests targeting `main`.
- Manual `workflow_dispatch`.

The core command matches the local release gate:

```sh
cmake --preset ci
cmake --build build/ci --target xgl_release_validation --parallel
```

This keeps GitHub validation aligned with local release validation.

## CI Jobs

| Job | Purpose | Main checks |
| --- | --- | --- |
| `release-validation` | Full release gate | CTest, cppcheck, SDK smoke, no-heap smoke, footprint, docs |
| `gcc-smoke` | Fast GCC build and test | `gcc-test` preset, examples, CTest |

`release-validation` uploads `build/ci/footprint/xgl-footprint.txt` as an artifact so MCU resource changes can be compared between runs.

## Documentation Deployment

`Deploy Docs` runs only on pushes to `main` or manual dispatch. It uses the official GitHub Pages artifact flow.

```sh
cmake --preset ci
cmake --build build/ci --target xgl_docs --parallel
```

The uploaded directory is:

```text
build/ci/docs/site
```

It contains the bilingual MkDocs site and the Doxygen public API reference.

## GitHub Pages Setup

Enable Pages in the GitHub repository:

1. Open repository `Settings`.
2. Go to `Pages`.
3. Set source to `GitHub Actions`.
4. Save and rerun `Deploy Docs`.

After deployment, the `github-pages` environment shows the published URL.

## Troubleshooting

| Failure point | Common cause | Action |
| --- | --- | --- |
| Configure fails | CMake preset or system dependency mismatch | Read the `cmake --preset ci` log |
| Static analysis fails | cppcheck found warning/style/performance/portability issues | Fix the source or add a justified inline suppression |
| Docs fail | MkDocs strict broken link or Doxygen configuration error | Run `cmake --build build/ci --target xgl_docs` locally |
| Tests fail | Protocol behavior regression or example API drift | Inspect CTest failure output |
| Pages fails | Pages source is not GitHub Actions, or workflow permissions are insufficient | Check repository Pages settings and workflow permissions |

## Release Rules

- Pull requests must pass `CI` before merge.
- A failed `main` docs deployment blocks release.
- Do not commit generated `site/` or Doxygen HTML output.
- Release candidates should keep the CI run, footprint artifact, and release validation record.
