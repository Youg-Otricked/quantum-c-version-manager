A Version & Package Manager for the QuantumC programming language.

---

## Overview

QCM has 2 sections, `tooling` and core.
Tooling commands install and update the compiler version, while core installs packages.

---

## Project Structure

QuantumC projects have this structure:

```graph

. MyProject
├─┐ dependencies/
│ ├┐ MyDependencyNumber1/
│ │└─ lib.qc
│ ╵
├─┐ docs/
│ ├─ index.md
│ ╵
├─┐ lib/
│ ├─ lib.md
│ ╵
├─┐ tests/
│ ├─ test.main.qc
│ ╵
├─ main.qc
│
├─ scope.yaml
╵
```

---

## Core Commands

1. `init`
        guides you through a TUI wizard for creating your project, makes the folder structure above.
2. `help`
        prints core help text
3. `upgrade`
        installs latest version of the CLI
4. `run`
        runs a command in your scope.yaml
5. `setup`
        sets up qcm (thing PATH and stuff)
6. `add`
        installs a package from a github url (or the registry if a offical package, but the registry is just shortend github links)
7. `remove`
        uninstalls a package.
8. `sync`
        changes qc version to current scopes version, installs all dependencies.

## Tooling Commands

All tooling commands are prefixed with `tooling`.

1. `help`
        prints tooling help text
2. `list-remote`
        lists all versions of qc, appending `[QCM TAGGED]` to important / major ones.
3. `install`
        installs a specific version of qc.
4. `uninstall`
        uninstalls a specific version of qc.
5. `list`
        lists all locally installed versions of qc, prefixing current with `*` and appending `[QCM TAGGED]` to important/major ones
6. `use`
        swaps current version of qc and its stdlib to that versions.
