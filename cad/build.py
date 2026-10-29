#!/usr/bin/env python3
"""Export fit prototypes and check closed, oriented STL topology; no pip packages."""
import collections
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
SCAD = ROOT / "charging-dock.scad"
OPENSCAD = os.environ.get("OPENSCAD") or shutil.which("openscad")
if not OPENSCAD:
    raise SystemExit("OpenSCAD is required (or set OPENSCAD to its executable).")


def run(name, part, bays=2, suffix="stl", extra=()):
    folder = ROOT / ("previews" if suffix == "png" else "exports")
    folder.mkdir(exist_ok=True)
    path = folder / f"{name}.{suffix}"
    command = [OPENSCAD, "--hardwarnings", "-D", f'part="{part}"', "-D", f"bays={bays}"]
    if suffix == "stl":
        command += ["--export-format", "binstl"]
    result = subprocess.run(command + list(extra) + ["-o", str(path), str(SCAD)],
                            capture_output=True, text=True)
    if result.returncode or not path.exists() or path.stat().st_size == 0:
        raise RuntimeError(f"Export failed: {name}\n{result.stdout}\n{result.stderr}")
    if "WARNING:" in result.stderr or "ERROR:" in result.stderr:
        raise RuntimeError(result.stderr)
    return path


def check_mesh(path):
    data = path.read_bytes()
    count, = struct.unpack_from("<I", data, 80)
    assert count > 0 and len(data) == 84 + count*50, f"Invalid binary STL: {path}"
    edges = collections.defaultdict(list)
    points = set()
    volume = 0
    for i in range(count):
        values = struct.unpack_from("<12fH", data, 84+i*50)
        a, b, c = [tuple(values[j:j+3]) for j in (3, 6, 9)]
        assert all(math.isfinite(v) for p in (a,b,c) for v in p)
        assert len({a,b,c}) == 3, f"Collapsed triangle: {path}"
        cross = ((b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]),
                 (b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]),
                 (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))
        assert sum(v*v for v in cross) > 0, f"Zero-area triangle: {path}"
        volume += (a[0]*(b[1]*c[2]-b[2]*c[1]) +
                   a[1]*(b[2]*c[0]-b[0]*c[2]) + a[2]*(b[0]*c[1]-b[1]*c[0]))/6
        points.update((a,b,c))
        for u,v in ((a,b),(b,c),(c,a)):
            edges[tuple(sorted((u,v)))].append((i, u < v))
    assert all(len(v) == 2 and v[0][1] != v[1][1] for v in edges.values()), \
        f"Open/nonmanifold/inconsistently wound edges: {path}"
    neighbours = [[] for _ in range(count)]
    for (a,_),(b,_) in edges.values():
        neighbours[a].append(b)
        neighbours[b].append(a)
    seen = {0}
    stack = [0]
    while stack:
        for i in neighbours[stack.pop()]:
            if i not in seen:
                seen.add(i)
                stack.append(i)
    assert len(seen) == count, f"Disconnected printable solid: {path}"
    assert volume > 0, f"Nonpositive volume: {path}"
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    assert abs(low[2]) < 0.001, f"Part does not sit on print bed: {path}"
    return dict(file=str(path.relative_to(ROOT)), triangles=count, connected_solids=1,
                closed_edges=True, consistent_winding=True,
                bounds_mm=[[round(x,3) for x in p] for p in (low,high)],
                size_mm=[round(b-a,3) for a,b in zip(low,high)], volume_mm3=round(volume,2))


if __name__ == "__main__":
    version = subprocess.check_output([OPENSCAD,"--version"], stderr=subprocess.STDOUT, text=True).strip()
    report = {"status":"TWO-BAY CLAMP AND CABLE-TAB PROTOTYPE - physical fit and retention unvalidated",
              "tool":version, "meshes":[], "parameter_checks":[]}
    for name,part,bays in [("dock-1-bay","dock",1),("dock-2-bay","dock",2),("fit-coupon","coupon",1)]:
        report["meshes"].append(check_mesh(run(name,part,bays)))
        print(f"Checked {name}",flush=True)
    for name,defs,valid in [
        ("offset-port", ["port_x=2", "port_y=-1"], True),
        ("eight-mm-insert", ["insert_l=8"], True),
        ("smaller-plug-body", ["plug_w=10", "plug_d=5"], True),
        ("larger-plug-body", ["plug_w=16", "plug_d=9"], True),
        ("large-cable-with-clearance", ["cable_d=5", "cable_slot_w=12", "cable_slot_h=14", "floor_z=36"], True),
        ("roof-too-low-for-side-loading", ["cable_slot_h=10"], False),
        ("straight-sided-base", ["base_scale=1"], True),
        ("narrower-bottom-rejected", ["base_scale=0.9"], False),
        ("tabs-block-side-loading", ["cable_slot_w=6"], False),
        ("bend-too-large-for-floor", ["cable_bend_r=14"], False),
        ("screw-exceeds-ten-mm", ["screw_l=12"], False),
        ("eight-mm-insert-small-plug-unreachable", ["insert_l=8", "plug_d=5"], False),
        ("invalid-bay-count", ["bays=3"], False),
        ("oversized-corner-radius", ["corner_r=20"], False),
        ("plug-body-too-tall", ["plug_body_h=40"], False)]:
        command = [OPENSCAD,"--hardwarnings","-D",'part="dock"']
        for definition in defs:
            command += ["-D",definition]
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)/"check.echo"
            result = subprocess.run(command+["-o",str(output),str(SCAD)],capture_output=True,text=True)
            diagnostic = result.stdout+result.stderr+(output.read_text() if output.exists() else "")
            errors = result.returncode != 0 or "ERROR:" in diagnostic
            assert errors != valid, f"Parameter check failed: {name}\n{diagnostic}"
        report["parameter_checks"].append({"case":name,"expected_valid":valid,"passed":True})
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp)/"cable-intersection.stl"
        result = subprocess.run([OPENSCAD,"--hardwarnings","-D",'part="cable-clearance"',
                                 "-o",str(output),str(SCAD)],capture_output=True,text=True)
        assert "Current top level object is empty" in result.stderr and not output.exists(), \
            f"Cable intersects dock or tabs:\\n{result.stderr}"
    report["cable_clearance_check"] = {"empty_intersection":True}
    for bays in [1,2]:
        run(f"dock-concept-{bays}-bay","assembly",bays,"png",
            ["--render","--imgsize","1400,1100","--projection","o","--colorscheme","Tomorrow",
             "--camera","95,-125,155,0,0,24","--viewall"])
    run("cable-tabs-underside","assembly",2,"png",
        ["--render","--imgsize","1500,1100","--projection","o","--colorscheme","Tomorrow",
         "--camera","100,120,-145,0,0,20","--viewall"])
    run("cable-exit-rear","dock",1,"png",
        ["--render","--imgsize","1000,700","--projection","o","--colorscheme","Tomorrow",
         "--camera","0,80,9,0,0,9"])
    (ROOT/"exports"/"validation.json").write_text(json.dumps(report,indent=2)+"\n")
    print("Simple dock exports, mesh checks and previews complete.")
