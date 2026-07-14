<!--
SPDX-License-Identifier: LGPL-3.0-or-later

Copyright (C) 2026, IBM. All rights reserved.
Author: Nishant Puri <Nishant.Puri1@ibm.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
for more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
-->

# bpftrace scripts for NFS-Ganesha

This directory holds [bpftrace](https://github.com/bpftrace/bpftrace) scripts
(`.bt` files) used to observe a running `ganesha.nfsd` without rebuilding or
restarting it. Each script attaches via uprobes to symbols in
`libganesha_nfsd.so` (not the thin `ganesha.nfsd` executable).

**Run on Linux only** — the host that runs Ganesha. eBPF is not available on
macOS or Windows.

## Available scripts

| Script | Purpose | Example command |
|--------|---------|-----------------|
| `mem_comp_audit.bt` | Detect alloc/free `mem_components_t` tag mismatches | `sudo bpftrace mem_comp_audit.bt "$LIB" 30` |

When you add a new `.bt` file, add a row to this table with a working example
command (including all required arguments).

## bpftrace positional arguments

Stock RHEL 9 bpftrace does **not** support truly optional positional parameters.
If a script references `$2` anywhere, bpftrace emits a warning when only one
argument is passed — even inside `if ($# >= 2)` blocks. There is no way to
default `$2` inside the `.bt` file without that warning.

**Workaround:** treat every positional argument as required. Pass **`30`** for
the usual heartbeat interval when you want the default.

## Finding `LIB` (the shared library path)

Scripts take the path to **`libganesha_nfsd.so`**, not `ganesha.nfsd`. Ganesha
ships three related names on disk:

| Name | Role |
|------|------|
| `libganesha_nfsd.so` | Symlink for development/linking — **do not pass this to bpftrace** |
| `libganesha_nfsd.so.NN` or `.NN.M` | Versioned file the dynamic linker maps (e.g. `.14.0`) |
| Path printed by `ldd` | **Use this as `$LIB`** — same file ganesha actually loads |

**Important:** bpftrace uprobes match the **exact path** you pass as `$1`. On a
source build, `libganesha_nfsd.so` is often a symlink to `libganesha_nfsd.so.14.0`.
Attaching to the symlink shows `Attached N probes` but **zero allocation events**
because ganesha executes code in the versioned file. Always set `LIB` from `ldd`
(or `readlink -f`), never hand-type the unversioned `.so` name.

Do **not** hard-code a version suffix (such as `.13.0`) in scripts or docs; it
changes with each Ganesha release. Always resolve `LIB` from the **same**
`ganesha.nfsd` binary you are running:

```bash
GANESHA=$(command -v ganesha.nfsd)
LIB=$(ldd "$GANESHA" | awk '/libganesha_nfsd/ {print $3; exit}')
# or: LIB=$(readlink -f "$BUILD_DIR/MainNFSD/libganesha_nfsd.so")
echo "$LIB"
```

If heartbeats show `allocation events=0  free events=0` while ganesha is running,
compare `$LIB` to `grep libganesha_nfsd /proc/$(pgrep ganesha.nfsd)/maps` — they
must be the same path (usually the versioned `.so.14.0`, not the symlink).

Optional sanity check (expect two lines with **`T`**):

```bash
nm -D "$LIB" | grep gsh_mem_stats_update
```

### Typical paths (package install)

After `dnf install nfs-ganesha` / `apt install nfs-ganesha` (exact paths vary
by distro and architecture):

| Component | Typical location |
|-----------|------------------|
| `ganesha.nfsd` | `/usr/bin/ganesha.nfsd` |
| `libganesha_nfsd.so` | `/usr/lib64/libganesha_nfsd.so` (RHEL x86_64) |
| Versioned `.so` | `/usr/lib64/libganesha_nfsd.so.14.0` (example — check yours) |

On Debian/Ubuntu the library is often under
`/usr/lib/x86_64-linux-gnu/libganesha_nfsd.so*`.

Even when you know the install prefix, prefer the `ldd` one-liner above so you
attach to the library the running process actually loads.

**Stripped packages:** many distro builds omit debug symbols. If `nm -D "$LIB"`
shows no `gsh_mem_stats_update` symbols, install the matching debuginfo package
(e.g. `nfs-ganesha-debuginfo` on RHEL) or use an unstripped build from source.

### Typical paths (build from source)

| Component | Typical location |
|-----------|------------------|
| `ganesha.nfsd` | `$BUILD_DIR/ganesha.nfsd` |
| `libganesha_nfsd.so` | `$BUILD_DIR/MainNFSD/libganesha_nfsd.so` (symlink — do not use for bpftrace) |
| Versioned `.so` | `$BUILD_DIR/MainNFSD/libganesha_nfsd.so.14.0` (what `ldd` prints — use this) |

Again, use `ldd "$BUILD_DIR/ganesha.nfsd"` rather than guessing the suffix.

## Prerequisites (all scripts)

1. **Linux** with BPF/uprobe support.

   ```bash
   uname -r
   ```

   RHEL 9 / kernel 5.14 is fine. You may see an informational "minimum 5.15"
   warning; uprobes still work if `Attached N probe(s)` follows.

2. **Root** (`sudo`). Loading eBPF programs requires elevated privileges.

3. **`bpftrace` installed**:

   ```bash
   # RHEL / Rocky / Alma / CentOS Stream
   sudo dnf install -y bpftrace

   # Debian / Ubuntu
   sudo apt update && sudo apt install -y bpftrace

   bpftrace --version
   ```

4. **Unstripped `libganesha_nfsd.so`** — symbols must be present for uprobes
   (see **Finding `LIB`** above).

5. **Matching Ganesha build** — the `.so` you pass must be the one loaded by
   the `ganesha.nfsd` process you are tracing.

## How to run any `.bt` script

```bash
cd /path/to/nfs-ganesha/src/scripts/bpftrace

GANESHA=$(command -v ganesha.nfsd)
LIB=$(ldd "$GANESHA" | awk '/libganesha_nfsd/ {print $3; exit}')

sudo bpftrace mem_comp_audit.bt "$LIB" 30
```

Slower heartbeat (60 seconds):

```bash
sudo bpftrace mem_comp_audit.bt "$LIB" 60
```

Stop any trace with **`Ctrl-C`**. Probes are removed immediately; Ganesha is
unchanged.

## Cold start (recommended for init-time tracing)

Many scripts miss allocations that happen before attach. For a clean first
run, start the trace **before** starting Ganesha:

```bash
# Terminal 1
cd /path/to/nfs-ganesha/src/scripts/bpftrace
GANESHA=$(command -v ganesha.nfsd)
LIB=$(ldd "$GANESHA" | awk '/libganesha_nfsd/ {print $3; exit}')
sudo bpftrace mem_comp_audit.bt "$LIB" 30

# Terminal 2 — start ganesha after the banner appears
"$GANESHA" -F -L /tmp/ganesha.log -f /path/to/ganesha.conf
```

If Ganesha is not installed yet, point `ldd` at your build-tree binary instead
of `command -v`.

Probes attach to the library **file path**, so they fire as soon as any
process loads that `.so`.

## Performance and safety

- **No overhead while not attached.** `Ctrl-C` removes all probes.
- **While attached**, each hit on a uprobe has a small cost. Under heavy NFS
  load this can be noticeable. Use for bounded test/staging sessions, not
  permanent production attachment.
- **Read-only observation** — these scripts do not modify Ganesha's memory or
  behaviour.

## Adding a new script

1. Add `your_script.bt` under this directory.
2. Add one row to **Available scripts** with purpose and a complete example
   command (all positional arguments required — no optional bpftrace `$N`).
