#!/usr/bin/env python3
# IW4Iris - static checks of the Director Mod's GSC scripts.
#
# This is NOT the game's script compiler (that one only runs inside the
# game, when a map loads); it catches the mistakes that compiler rejects
# with "script compile error: bad syntax" or "unknown function" before they
# reach a user:
#   * non-ASCII bytes (the files must be plain ASCII, comments included);
#   * a reserved word (anim, level, self, game, ...) used as a parameter or
#     assigned to as a variable - the cause of the first in-game report;
#   * constructs IW4's compiler does not have (?:, foreach, default
#     parameter values);
#   * unbalanced brackets;
#   * calls to IW4Iris builtins the client does not register, and calls to
#     functions defined nowhere (neither in the mod nor in the list of
#     engine functions the mod is known to use).
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOD = ROOT / "mods" / "iw4iris_director"
BRIDGE = ROOT / "engine" / "iw4x" / "src" / "IrisDirector.cpp"

# Words the IW4 script compiler reserves.
RESERVED = {
    "if", "else", "while", "for", "switch", "case", "default", "break", "continue", "return",
    "wait", "waittill", "waittillmatch", "waittillframeend", "endon", "notify", "thread",
    "size", "self", "level", "game", "anim", "undefined", "true", "false",
    "breakpoint", "prof_begin", "prof_end", "isdefined",
}

# Engine (stock IW4 multiplayer) functions and methods the mod calls. A call
# to anything else that the mod does not define fails this test, so a typo
# or a function of another game cannot slip in. Extend this list only with
# functions shown to exist in IW4 multiplayer. Evidence for each (calls in
# public IW4 scripts, checked 2026-11-26):
#   IW4Cine (sortileges/iw4cine, "Cinematic mod/scripts/actors.gsc" and
#   others): scriptModelPlayAnim, precacheMPAnim, getWeaponHideTags,
#   getWeaponModel, hidePart, enableLinkTo, linkTo, getTagOrigin,
#   getTagAngles, spawnStruct, loadFX, playFX, strTok;
#   stock MW2 scripts (iw4x/iw4x-rawfiles, iw4x_00/maps/mp/...):
#   closepopupMenu, closeInGameMenu (_menus.gsc), getEntityNumber
#   (_playerlogic.gsc), getArrayKeys (_utility.gsc);
#   long-standing IW-engine basics used throughout those scripts: isDefined,
#   spawn, setModel, unlink, delete, hide, show, int, getTime, precacheModel,
#   precacheItem.
ENGINE = {
    "spawnstruct", "strtok", "precachemodel", "precachempanim", "precacheitem", "loadfx",
    "isdefined", "spawn", "setmodel", "gettagorigin", "gettagangles", "enablelinkto", "linkto",
    "unlink", "delete", "getentitynumber", "scriptmodelplayanim", "getweaponmodel",
    "getweaponhidetags", "hidepart", "hide", "show", "getarraykeys", "int", "gettime", "playfx",
    "closepopupmenu", "closeingamemenu",
}


def strip(source):
    """Source with comments and string contents blanked (lengths kept)."""
    out = []
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if source.startswith("//", i):
            j = source.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in source[i:j]))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and source[j] != '"':
                j += 2 if source[j] == "\\" else 1
            out.append('"' + " " * (min(j, n) - i - 1) + '"')
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def scripts():
    return sorted(MOD.rglob("*.gsc"))


def defined_functions():
    names = set()
    for path in scripts():
        code = strip(path.read_text(encoding="ascii", errors="replace"))
        depth = 0
        for m in re.finditer(r"[{}]|^([A-Za-z_]\w*)\s*\(", code, re.M):
            tok = m.group(0)
            if tok == "{":
                depth += 1
            elif tok == "}":
                depth -= 1
            elif depth == 0:
                names.add(m.group(1).lower())
    return names


def registered_builtins():
    return {m.lower() for m in re.findall(r'AddFunction\("(\w+)"', BRIDGE.read_text(encoding="utf-8"))}


class GscTests(unittest.TestCase):
    def test_scripts_exist(self):
        self.assertTrue(scripts(), "no .gsc files found")

    def test_ascii_only(self):
        for path in scripts():
            data = path.read_bytes()
            bad = [i for i, b in enumerate(data) if b > 127]
            self.assertFalse(bad, f"{path.name}: non-ASCII byte at offset {bad[:1]}")

    def test_balanced_brackets(self):
        pairs = {")": "(", "]": "[", "}": "{"}
        for path in scripts():
            code = strip(path.read_text(encoding="ascii"))
            stack = []
            for i, ch in enumerate(code):
                if ch in "([{":
                    stack.append((ch, i))
                elif ch in pairs:
                    self.assertTrue(stack and stack[-1][0] == pairs[ch],
                                    f"{path.name}:{line_of(code, i)}: unbalanced '{ch}'")
                    stack.pop()
            self.assertFalse(stack, f"{path.name}: unclosed '{stack[-1][0] if stack else ''}'")

    def test_no_reserved_word_as_name(self):
        for path in scripts():
            code = strip(path.read_text(encoding="ascii"))
            # Parameters of function definitions (top level).
            for m in re.finditer(r"^([A-Za-z_]\w*)\s*\(([^)]*)\)\s*\{", code, re.M):
                for param in [p.strip() for p in m.group(2).split(",") if p.strip()]:
                    self.assertRegex(param, r"^[A-Za-z_]\w*$",
                                     f"{path.name}:{line_of(code, m.start())}: parameter '{param}' of "
                                     f"{m.group(1)}() is not a plain name (no default values in IW4)")
                    self.assertNotIn(param.lower(), RESERVED,
                                     f"{path.name}:{line_of(code, m.start())}: parameter '{param}' of "
                                     f"{m.group(1)}() is a reserved word")
            # Assignments to a bare reserved word ("anim = ...", "level = ...").
            for m in re.finditer(r"(?<![\w.\]])([A-Za-z_]\w*)\s*(?:[-+*/]?=)(?!=)", code):
                self.assertNotIn(m.group(1).lower(), RESERVED,
                                 f"{path.name}:{line_of(code, m.start())}: assignment to reserved word "
                                 f"'{m.group(1)}'")
            # Reserved words used as a variable passed around ("f( anim )").
            for m in re.finditer(r"[(,]\s*(anim)\s*[),]", code):
                self.fail(f"{path.name}:{line_of(code, m.start())}: '{m.group(1)}' used as a variable")

    def test_no_constructs_iw4_lacks(self):
        for path in scripts():
            code = strip(path.read_text(encoding="ascii"))
            for word, why in ((r"\?", "the ?: operator"), (r"\bforeach\b", "foreach")):
                m = re.search(word, code)
                self.assertIsNone(m, f"{path.name}:{line_of(code, m.start()) if m else 0}: {why} is not IW4 GSC")

    def test_calls_resolve(self):
        defined = defined_functions()
        builtins = registered_builtins()
        self.assertIn("iw4irisready", builtins, "could not read the builtins from IrisDirector.cpp")
        for path in scripts():
            code = strip(path.read_text(encoding="ascii"))
            depth = 0
            for m in re.finditer(r"[{}]|(?:(\w+(?:\\\w+)*)::)?\b([A-Za-z_]\w*)\s*\(", code):
                tok = m.group(0)
                if tok == "{":
                    depth += 1
                    continue
                if tok == "}":
                    depth -= 1
                    continue
                if depth == 0:
                    continue  # a definition, not a call
                name = m.group(2).lower()
                if name in RESERVED - {"isdefined"}:
                    continue  # if ( ... ), while ( ... ), switch ( ... ) ...
                where = f"{path.name}:{line_of(code, m.start())}"
                if name.startswith("iw4iris"):
                    self.assertIn(name, builtins, f"{where}: {m.group(2)}() is not registered by the client")
                elif m.group(1):
                    target = MOD / (m.group(1).replace("\\", "/") + ".gsc")
                    self.assertTrue(target.exists(), f"{where}: {m.group(1)}.gsc is not part of the mod")
                else:
                    self.assertTrue(name in defined or name in ENGINE,
                                    f"{where}: {m.group(2)}() is defined nowhere (not in the mod, not a "
                                    f"known IW4 function)")

    def test_manifest_lists_every_script(self):
        import json
        manifest = json.loads((MOD / "mod.json").read_text(encoding="utf-8"))
        files = set(manifest["install"]["files"])
        on_disk = {p.relative_to(MOD).as_posix() for p in scripts()}
        self.assertEqual(files, on_disk)

    def test_checker_catches_the_reported_bug(self):
        # The construct from the first in-game report must be caught.
        code = strip("playAnimation( requestId, objectId, anim )\n{\n    x = 1;\n}\n")
        m = re.search(r"^([A-Za-z_]\w*)\s*\(([^)]*)\)\s*\{", code, re.M)
        params = [p.strip().lower() for p in m.group(2).split(",")]
        self.assertIn("anim", RESERVED & set(params))


if __name__ == "__main__":
    sys.exit(0 if unittest.main(exit=False).result.wasSuccessful() else 1)
