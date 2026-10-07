# RTTI fixtures: official protoc / flatc

Golden binary fixtures produced by the official Google `protoc` and FlatBuffers
`flatc` toolchains. Used as ground truth for round-trip tests across the proto
v2/v3 and FlatBuffers interop work.

## Tools

| Tool  | Version   | Default path (per-user install)                      |
|-------|-----------|------------------------------------------------------|
| protoc| 36.2      | `%USERPROFILE%\.rtti-tools\protoc\bin\protoc.exe`    |
| flatc | 25.12.19  | `%USERPROFILE%\.rtti-tools\flatc\flatc.exe`          |

The tools are **not** part of this repository. Regeneration is optional;
the generated `.wire` / `.bin` / `*_generated.h` files are committed so tests
run without the toolchain installed.

## Regenerating

```powershell
powershell -ExecutionPolicy Bypass -File scripts\gen_fixtures.ps1
```

Custom tool paths:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\gen_fixtures.ps1 `
  -Proto "C:\tools\protoc.exe" -Flatc "C:\tools\flatc.exe"
```

The script:

- encodes every `tests/fixtures/proto/<B>.textbin` / `<B>.<Message>.textbin`
  testcase with `protoc --encode=rtti.fixt.<Message>` into `\<B>.<Message>.wire`
  (for a bare `<B>.textbin` the first message declared in `<B>.proto` is used),
- decodes each `.wire` back with `--decode` and asserts the expected values,
- generates `<F>_generated.h` (`flatc --cpp`) for every `tests/fixtures/flat/*.fbs`,
- converts every `tests/fixtures/flat/*.json` to `*.bin` with
  `flatc --binary`, using the schema's `root_type`.

## Layout

```
tests/fixtures/
  proto/
    basic.proto                 # schema (proto3)
    basic.textbin               # text-format input for message Scalars
    basic.Outer.textbin         # text-format input for nested message Outer
    basic.Scalars.wire          # official protoc encode(Scalars) binary
    basic.Outer.wire            # official protoc encode(Outer) binary
  flat/
    basic.fbs                   # schema (flatbuffers)
    monster.json                # JSON input (root Monster)
    monster.bin                 # official flatc --binary output
    basic_generated.h           # official flatc --cpp output
```

Conventions:

- Description files (`.proto`, `.fbs`, `.textbin`, `.json`, this README) are
  the source of truth.
- Generated artifacts (`.wire`, `.bin`, `*_generated.h`) are committed as fixed
  baselines and must only change via `scripts\gen_fixtures.ps1` upstream.
- Message type names in textbin filenames must match `message` declarations
  in the `.proto` (e.g. `basic.Outer.textbin` -> `rtti.fixt.Outer`).