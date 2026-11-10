#!/usr/bin/env python3
"""check_copy.py: is every line of synthcore a line of synthshell, unchanged?

    python3 tools/check_copy.py            # check include/core against synthshell@SOURCE
    python3 tools/check_copy.py --selftest # plant a changed line and a moved line; both must fail

synthcore is a cut-down COPY of synthshell. The rule for a copy is that nothing
in it is changed before it is proved, so this checks that each file here is the
source with lines removed and nothing else: every line, after undoing the
declared renames, appears in the source, in the same order. The only lines
allowed to be new are the ones listed in ADDED, which are this package's own
header and doc comment. The source is read from synthshell's git history at a
fixed commit, so a later change in synthshell cannot make this pass or fail.
"""
import subprocess, sys, pathlib

SOURCE_REPO = pathlib.Path.home() / "projects" / "synthshell"
SOURCE = "53e9a9a"
FILES = {  # copy -> source
    "include/core/MidiClock.h":    "include/shell/MidiClock.h",
    "include/core/TempoSync.h":    "include/shell/TempoSync.h",
    "include/core/QwertyPlayer.h": "include/shell/QwertyPlayer.h",
    "include/core/CoreHost.h":     "include/shell/ShellHost.h",
}
# The renames, applied backwards to a copy's line before it is looked up.
RENAMES = [("namespace core {", "namespace shell {"), ("} // namespace core", "} // namespace shell"),
           ("CoreHost", "ShellHost")]
# Lines that are synthcore's own, by file. Nothing else may be new.
ADDED = {
    # The 8-line header and the 2-line class doc: 10 lines, of which the bare
    # "//" also occurs in the source and so matches there. 9 is exact.
    "include/core/CoreHost.h": 12,   # + 3: INT ignores a playing host and MIDI clock (CUTS.md, changed after the proof)
    "include/core/MidiClock.h": 17,  # B-008: each MIDI clock pulse at its true sample (CUTS.md, changed after the proof)
}

root = pathlib.Path(__file__).resolve().parent.parent

def source_lines(path):
    r = subprocess.run(["git", "-C", str(SOURCE_REPO), "show", f"{SOURCE}:{path}"],
                       capture_output=True, text=True)
    if r.returncode: sys.exit(f"cannot read {path} at synthshell@{SOURCE}: {r.stderr.strip()}")
    return r.stdout.split("\n")

def unrename(line):
    for new, old in RENAMES: line = line.replace(new, old)
    return line

def check(copy, src, allowed_new):
    """Returns (kept, new, problems). Greedy in-order match: each copy line must be
    found in the source after the previous match."""
    i, new, problems = 0, 0, []
    for n, line in enumerate(copy, 1):
        want = unrename(line)
        j = i
        while j < len(src) and src[j] != want: j += 1
        if j < len(src): i = j + 1; continue
        new += 1
        if new > allowed_new:
            problems.append(f"line {n} is not a line of the source (or is out of order): {line.strip()[:80]!r}")
    return len(copy) - new, new, problems

def main():
    bad = 0
    for cp, sp in FILES.items():
        copy = (root / cp).read_text().split("\n"); src = source_lines(sp)
        kept, new, problems = check(copy, src, ADDED.get(cp, 0))
        print(f"  {cp}: {kept} lines from {sp} ({len(src)}), {new} new (allowed {ADDED.get(cp, 0)})"
              + ("" if not problems else "  FAILS"))
        for p in problems: print("      " + p)
        bad += bool(problems)
    print(f"\n  {len(FILES)} files, {bad} failing (source: synthshell@{SOURCE})")
    return 1 if bad else 0

def selftest():
    cp, sp = "include/core/MidiClock.h", FILES["include/core/MidiClock.h"]
    copy = (root / cp).read_text().split("\n"); src = source_lines(sp)
    allowed = ADDED.get(cp, 0)      # the file's declared new lines (B-008): the planted fault must be one more
    wrong = 0
    # 1. one character changed in a kept line of code
    k = next(n for n, l in enumerate(copy) if "{" in l and not l.strip().startswith("//") and "namespace" not in l)
    changed = copy[:]; changed[k] = changed[k].replace("{", "{ ", 1)
    wrong += not check(changed, src, allowed)[2]
    # 2. two kept lines swapped
    a = next(n for n, l in enumerate(copy) if l.strip() and l.strip() != copy[n + 1].strip())
    swapped = copy[:]; swapped[a], swapped[a + 1] = swapped[a + 1], swapped[a]
    wrong += not check(swapped, src, allowed)[2]
    # 3. the true copy must pass
    wrong += bool(check(copy, src, allowed)[2])
    print(f"check_copy selftest: 3 cases, {wrong} wrong (a changed character, two lines swapped, the true copy)")
    return 1 if wrong else 0

if __name__ == "__main__":
    sys.exit(selftest() if "--selftest" in sys.argv else main())
