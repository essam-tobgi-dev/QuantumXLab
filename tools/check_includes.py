#!/usr/bin/env python3
"""Layering lint (spec 02 §1).

G1: layers 0-3 must not include OpenGL, GLFW or ImGui headers — they compile and test headless.
P1: only the presentation layers may call ImGui directly.
Dependency rule: a module may include headers of the modules it links, and no others.

Run from anywhere; exits non-zero and prints every violation.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# Directory -> the module directories it is allowed to include from (transitively closed below).
DEPS = {
    "Core": [], "Units": ["Core"],
    "Numerics": ["Core", "Units"], "Data": ["Core", "Units"],
    "QSim": ["Numerics", "Core", "Units"],
    "Hardware": ["Numerics", "Units", "Core"],
    "Noise": ["QSim", "Hardware", "Numerics", "Core", "Units", "Data"],
    "Pulse": ["Hardware", "Numerics", "Core", "Units", "QSim"],
    "Cryo": ["Hardware", "Data", "Core", "Units", "Numerics"],
    "Lang": ["Core"],
    "IR": ["Numerics", "Lang", "Core", "Units", "QSim"],
    "Compiler": ["IR", "Hardware", "Pulse", "Lang", "Numerics", "Core", "Units", "QSim"],
    "QEC": ["IR", "QSim", "Core", "Numerics", "Units", "Data", "Lang"],
    "Runtime": ["Compiler", "QSim", "Noise", "Cryo", "Data", "QEC", "IR", "Lang",
                "Hardware", "Pulse", "Numerics", "Core", "Units"],
    "Instruments": ["Pulse", "Cryo", "Data", "QSim", "Noise", "Hardware", "Numerics", "Core", "Units"],
    "Graphics": ["Core", "Units"],
    "Lab": ["Graphics", "Hardware", "Cryo", "Data", "Core", "Units", "Numerics"],
    "Viz": ["Graphics", "Lab", "QSim", "IR", "Pulse", "Hardware", "QEC", "Data", "Core",
            "Units", "Numerics"],
    "UI": ["Viz", "Lab", "Runtime", "Instruments", "Lang", "Compiler", "Cryo", "Data",
           "Graphics", "QSim", "IR", "Pulse", "Hardware", "Core", "Units", "Numerics", "QEC"],
    "Report": ["Runtime", "Data", "Graphics", "Compiler", "IR", "Lang", "QSim", "Pulse",
               "Hardware", "Cryo", "Core", "Units", "Numerics", "QEC", "Instruments"],
    "App": None,  # the composition root may include anything
}

# Layers 0-3: headless, no graphics stack (rule G1).
HEADLESS = {"Core", "Units", "Numerics", "Data", "QSim", "Noise", "Hardware", "Pulse",
            "Cryo", "Lang", "IR", "Compiler", "QEC", "Runtime", "Instruments"}
GRAPHICS_HEADERS = re.compile(r'[<"](GL/|OpenGL/|GLFW/|glad|imgui|implot|glm/)', re.I)
IMGUI_CALL = re.compile(r'\bImGui::|\bImPlot::')
# Only these may call ImGui/ImPlot directly (rule P1).
IMGUI_OK = {"UI", "Viz", "App"}
INCLUDE = re.compile(r'^\s*#\s*include\s+([<"])([^">]+)[">]', re.M)

def module_of(path):
    rel = os.path.relpath(path, SRC)
    return rel.split(os.sep)[0]

def main():
    problems = []
    for dirpath, _dirs, files in os.walk(SRC):
        for f in files:
            if not f.endswith((".cpp", ".hpp", ".inc")):
                continue
            p = os.path.join(dirpath, f)
            mod = module_of(p)
            if mod.endswith(".cpp"):      # src/main.cpp
                mod = "App"
            text = open(p, encoding="utf8", errors="ignore").read()
            rel = os.path.relpath(p, ROOT)

            if mod in HEADLESS:
                for line in text.splitlines():
                    if line.lstrip().startswith("//"):
                        continue
                    if GRAPHICS_HEADERS.search(line) and INCLUDE.match(line):
                        problems.append(f"{rel}: G1 — layer 0-3 module includes a graphics header: {line.strip()}")
            if mod not in IMGUI_OK:
                for n, line in enumerate(text.splitlines(), 1):
                    if IMGUI_CALL.search(line) and not line.lstrip().startswith("//"):
                        problems.append(f"{rel}:{n}: P1 — ImGui/ImPlot called outside UI/Viz/App")
                        break

            allowed = DEPS.get(mod)
            if allowed is None:
                continue
            for m in INCLUDE.finditer(text):
                inc = m.group(2)
                head = inc.split("/")[0]
                if head in DEPS and head != mod and head not in allowed:
                    problems.append(f"{rel}: dependency rule — {mod} includes {inc}, "
                                    f"but {mod} does not link {head}")
    for p in sorted(set(problems)):
        print(p)
    print(f"\n{len(set(problems))} violation(s)")
    return 1 if problems else 0

if __name__ == "__main__":
    sys.exit(main())
