# Contribute to xgen-link

Read the [engineering conventions](docs/standards.md) and the affected API, protocol or architecture page. Use the [development workflow](docs/en/development/workflow.md) to prepare tools, run checks and record evidence. 中文入口为[开发工作流](docs/zh/development/workflow.md)。

## Propose a change

1. Describe the user-visible behavior and affected profiles. Keep source, configuration and hardware assumptions explicit.
2. For a behavior change or defect, reproduce the failing case through the affected production path before the fix. Preserve existing test names, labels and seeds.
3. Build and run the matching checks. Use the [validation guide](docs/en/development/validation.md) for SDK, protocol and resource checks.
4. Update the canonical contract and its paired translation. Run the [documentation checks](docs/en/development/documentation.md) and review the generated reference if an API changed.

For bug reports, include the source revision, build profile, toolchain, relevant configuration, reproduction steps and original results. For a resource change, include comparable target artifacts and the runtime capacities.
