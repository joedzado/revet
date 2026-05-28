# revet docs

Topical docs that don't fit in the project README's tour.

## Getting started

- [install.md](install.md) — prereqs and installer per OS
- [config.md](config.md) — `revet.toml`, env vars, CLI flags
- [troubleshooting.md](troubleshooting.md) — when something goes wrong

## Going to production

- [security.md](security.md) — pre-deploy checklist, key gen + rotation, what revet protects against
- [deploy.md](deploy.md) — Docker, systemd, nginx reverse proxy, signals, logging
- [upgrade.md](upgrade.md) — framework + schema migrations between versions

## Hacking on the framework

- [contributing.md](contributing.md) — dev loop, tests, conventions, release process
- [architecture.md](architecture.md) — build + runtime pipeline, key abstractions

## Reference

- [codegen.md](codegen.md) — every script in `cmake/gen_*.py`
- [attributes.md](attributes.md) — `[[revet::*]]` markers
- [api.md](api.md) — runtime types and helpers

For the tour + feature walkthrough, see the [project README](../README.md).
