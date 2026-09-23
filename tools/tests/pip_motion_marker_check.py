"""Check per-draw reset integration and compile the owner overlay against its baseline."""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ENGINE = Path(__file__).resolve().parents[2]
AUTHORED = Path("D:/ANTHOLOGY_DEV/addons/Anthology PiP Rework/gamedata/shaders")
BASELINE = ENGINE / "_build/shader_validation/v140"
OUT = ENGINE / "_build/pip-motion-validation/marker"
FXC = Path("C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe")


def active_providers():
    mo2 = ENGINE.parent / "SYS_A.N.T.H.O.L.O.G.Y_mo2_CBT"
    ini = (mo2 / "ModOrganizer.ini").read_text(encoding="utf-8-sig")
    raw = re.search(r"selected_profile=@ByteArray\((.*)\)", ini).group(1)
    profile = re.sub(r"\\x([0-9a-fA-F]{1,2})", lambda m: chr(int(m.group(1), 16)), raw).encode("latin1").decode("utf8")
    mods = [line[1:] for line in (mo2 / "profiles" / profile / "modlist.txt").read_text(encoding="utf-8-sig").splitlines() if line.startswith("+")]
    providers = {}
    for mod in mods:  # MO2 modlist is highest priority first.
        folder = mo2 / "mods" / mod / "gamedata/shaders"
        if folder.is_dir():
            for path in folder.rglob("*"):
                if path.is_file() and path.suffix.lower() in (".h", ".ps", ".vs", ".cs"):
                    providers.setdefault(path.relative_to(folder).as_posix().lower(), path)
    (OUT / "active_providers.json").write_text(json.dumps({name: dict(source=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        for name, path in providers.items()}, indent=2, ensure_ascii=False), encoding="utf8")
    return providers


def check_integration():
    graph = (ENGINE / "src/Layers/xrRender/r__dsgraph_render.cpp").read_text()
    sorted_draw = graph[graph.index("void CDSGraphManager::r_dsgraph_render_graph_sorted"):graph.index("void CDSGraphManager::r_dsgraph_render_graph(")]
    assert sorted_draw.index("RCache.set_Element") < sorted_draw.index("PipMotionHistoryScope") < sorted_draw.index("V->Render")
    packet_draw = graph[graph.index("void CDSGraphManager::r_dsgraph_render_graph("):graph.index("void CDSGraphManager::r_dsgraph_render_hud")]
    assert packet_draw.index("RCache.set_Constants") < packet_draw.index("PipMotionHistoryScope") < packet_draw.index("item.pVisual->Render")
    for queue in ("Sorted", "Emissive", "Wmark", "Distort"):
        assert re.search(r"r_dsgraph_render_graph_sorted\(RGraph\.mapStaticSorted\." + queue + r", (?:true|clear), true\)", graph)
    skeleton = (ENGINE / "src/Layers/xrRender/SkeletonX.cpp").read_text()
    body = skeleton[skeleton.index("void CSkeletonX::_Render("):skeleton.index("void CSkeletonX::_Render_soft(")]
    assert body.index("motionHistoryEpoch.Begin") < body.index("PipMotionHistoryScope") < body.index("switch (RenderMode)")
    assert "PreviousValid() && RenderMode != RM_SKINNING_SOFT" in body
    history = (ENGINE / "src/Layers/xrRender/PipMotionHistory.h").read_text()
    assert "state.unknown, state.owner, 0.f, 0.f" in history
    assert "owner ? owner->GetRenderSurfaceOwnerId() : 0.f" in history
    print("PASS per-packet/child binding order and static/dynamic classification", flush=True)


def diagnostic_counts(output):
    return Counter(re.findall(r"warning (X\d+): ([^\r\n]*)", output))


def compile_case(folder, file, profile, macros, name):
    # Existing model vertex shaders retain FXVS/DX9 technique declarations.
    # Match the engine's non-strict compilation rather than rewriting providers.
    command = [str(FXC), "/nologo", "/T", profile, "/E", "main"]
    for macro in macros:
        command += ["/D", macro]
    command += ["/Fo", str(OUT / (name + ".cso")), str(folder / file)]
    result = subprocess.run(command, capture_output=True)
    output = (result.stdout + result.stderr).decode("utf8", errors="replace")
    (OUT / (name + ".log")).write_text(output, encoding="utf8")
    return result.returncode, diagnostic_counts(output)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    check_integration()
    providers = active_providers()
    rows = []
    pixel_cases = ["deffer_base_bump", "deffer_base_flat", "deffer_base_aref_bump",
                   "deffer_base_aref_flat", "deffer_grass", "deffer_tree", "deffer_tree_bump"]
    for renderer, version in (("r3", "4_0"), ("r4", "5_0")):
        baseline = OUT / "baseline" / renderer
        shutil.copytree(BASELINE / renderer, baseline, dirs_exist_ok=True)
        # The older lens fixture did not need the motion MRT declaration. Use
        # the current winning MO2 files for full geometry shader validation.
        for source_renderer in (("r3",) if renderer == "r3" else ("r3", "r4")):
            for name, provider in providers.items():
                if name.startswith(source_renderer + "/"):
                    destination = baseline / name[3:]
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(provider, destination)
        overlay = OUT / renderer
        shutil.copytree(baseline, overlay, dirs_exist_ok=True)
        header = AUTHORED / renderer / "screenspace_mvectors.h"
        shutil.copy2(header, overlay / header.name)
        macros = ["USE_DX11=1", "GBUFFER_OPTIMIZATION=1", "SSFX_MODEXE=1"]
        cases = [(stem + ".ps", "ps_" + version, macros) for stem in pixel_cases if (baseline / (stem + ".ps")).exists()]
        cases += [("deffer_model_bump.vs", "vs_" + version, macros + ["SKIN_" + skin + "=1"]) for skin in ("NONE", "0", "1", "2", "3", "4")]
        cases += [("deffer_base_bump.ps", "ps_" + version, macros + ["USE_MSAA=1", "MSAA_SAMPLES=4"])]
        for i, (file, profile, defines) in enumerate(cases):
            name = renderer + "_" + str(i) + "_" + file.replace(".", "_")
            old_exit, old_warnings = compile_case(baseline, file, profile, defines, name + "_baseline")
            new_exit, new_warnings = compile_case(overlay, file, profile, defines, name + "_owner")
            introduced = new_warnings - old_warnings
            passed = old_exit == new_exit == 0 and not introduced
            row = dict(name=name, source=file, defines=defines, baseline_exit=old_exit, overlay_exit=new_exit,
                       baseline_warnings=sum(old_warnings.values()), overlay_warnings=sum(new_warnings.values()),
                       new_warnings=[list(k) + [v] for k, v in introduced.items()], passed=passed,
                       header_sha256=hashlib.sha256(header.read_bytes()).hexdigest())
            rows.append(row)
            print(json.dumps(row), flush=True)
    (OUT / "compile_summary.json").write_text(json.dumps(rows, indent=2), encoding="utf8")
    assert all(row["passed"] for row in rows), "Shader regression; inspect marker logs"


if __name__ == "__main__":
    main()
