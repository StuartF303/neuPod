"""Render PNGs (off-screen VTK) to mech/renders/:
    <layout>_iso.png, _top.png, _section_side.png (cut at the wheel centre X, viewed from the left),
    _section_wheel.png (cut at the wheel centre Y, viewed from the bottom), _exploded.png

    python -m mech.render            # all layouts
    python -m mech.render D2 stock   # some
Run under xvfb-run if VTK complains about a display.
"""
import sys
from pathlib import Path

import vtk
import cadquery as cq

from . import params as p
from .assembly import build
from .parts.common import box

RENDERS = Path(__file__).resolve().parent / "renders"
SIZE = (1400, 1000)
SIZE_SECTION = (1600, 560)
EXPLODE = {"shell": -14.0, "candidate_back": -8.0, "frame": 0.0, "board": 4.0, "front": 9.0, "front_plate": 16.0}


def _polydata(shape):
    verts, tris = shape.val().tessellate(0.08, 0.3)
    pts = vtk.vtkPoints()
    for v in verts:
        pts.InsertNextPoint(v.x, v.y, v.z)
    cells = vtk.vtkCellArray()
    for t in tris:
        cells.InsertNextCell(3, t)
    pd = vtk.vtkPolyData()
    pd.SetPoints(pts)
    pd.SetPolys(cells)
    n = vtk.vtkPolyDataNormals()
    n.SetInputData(pd)
    n.SetFeatureAngle(40)
    n.Update()
    return n.GetOutput()


def _actor(shape, colour, opacity=1.0):
    m = vtk.vtkPolyDataMapper()
    m.SetInputData(_polydata(shape))
    a = vtk.vtkActor()
    a.SetMapper(m)
    a.GetProperty().SetColor(*colour)
    a.GetProperty().SetOpacity(opacity)
    a.GetProperty().SetSpecular(0.15)
    a.GetProperty().SetAmbient(0.25)
    a.GetProperty().SetDiffuse(0.75)
    return a


def _explode_dz(part):
    if part.name == "front_plate":
        return EXPLODE["front_plate"]
    if part.group == "candidate" and part.bbox[5] <= p.RIB_Z_BACK + 0.2:
        return EXPLODE["candidate_back"]
    if part.group == "candidate":
        return EXPLODE["board"]
    if part.group == "cavity":
        return EXPLODE["candidate_back"]
    return EXPLODE.get(part.group, 0.0)


def _cut(shape, keep):
    """keep: ('x', x0) keeps x <= x0 (viewed from +X so +Y is to the right); ('y', y0) keeps y >= y0."""
    big = 400
    if keep[0] == "x":
        cutter = box(keep[1], -big, -big, big, big, big)
    else:
        cutter = box(-big, -big, -big, big, keep[1], big)
    try:
        return shape.cut(cutter)
    except Exception:
        return shape


def render(layout, view, out):
    parts, _ = build(layout)
    ren = vtk.vtkRenderer()
    ren.SetBackground(1, 1, 1)
    bounds = [1e9, -1e9, 1e9, -1e9, 1e9, -1e9]
    for pt in parts:
        shape = pt.shape
        opacity = 1.0
        if view == "section_side":
            shape = _cut(shape, ("x", p.WHEEL_CX))
        elif view == "section_wheel":
            shape = _cut(shape, ("y", p.WHEEL_CY))
        elif view == "exploded":
            shape = shape.translate((0, 0, _explode_dz(pt)))
        if view in ("iso", "top") and pt.name in ("front_plate",):
            opacity = 0.25
        if view in ("iso", "top") and pt.name in ("amoled", "lcd_stock", "click_wheel"):
            opacity = 0.6
        if view == "top" and pt.name == "back_shell":
            continue
        if pt.group == "region":
            opacity = 0.3
        try:
            if shape.val().Volume() < 1e-6:
                continue
        except Exception:
            continue
        a = _actor(shape, pt.colour, opacity)
        ren.AddActor(a)
        b = a.GetBounds()
        bounds = [min(bounds[0], b[0]), max(bounds[1], b[1]), min(bounds[2], b[2]), max(bounds[3], b[3]),
                  min(bounds[4], b[4]), max(bounds[5], b[5])]
    cam = ren.GetActiveCamera()
    cx, cy, cz = [(bounds[i] + bounds[i + 1]) / 2 for i in (0, 2, 4)]
    cam.SetFocalPoint(cx, cy, cz)
    if view == "top":
        cam.SetPosition(cx, cy, cz + 400); cam.SetViewUp(0, 1, 0); cam.ParallelProjectionOn()
    elif view == "section_side":
        cam.SetPosition(cx + 400, cy, cz); cam.SetViewUp(0, 0, 1); cam.ParallelProjectionOn()
    elif view == "section_wheel":
        cam.SetPosition(cx, cy - 400, cz); cam.SetViewUp(0, 0, 1); cam.ParallelProjectionOn()
    else:  # iso / exploded: from front-top-right
        cam.SetPosition(cx + 180, cy - 260, cz + 260); cam.SetViewUp(0, 0, 1)
        if view == "exploded":
            cam.SetPosition(cx + 220, cy - 300, cz + 200)
    ren.ResetCamera(bounds)
    size = SIZE_SECTION if view.startswith("section") else SIZE
    aspect = size[0] / size[1]
    if view == "top":
        cam.SetParallelScale((bounds[3] - bounds[2]) * 0.52)
    elif view == "section_side":
        cam.SetParallelScale((bounds[3] - bounds[2]) / 2 / aspect * 1.08)
        cam.SetFocalPoint(cx, cy, cz + 11); cam.SetPosition(cx + 400, cy, cz + 11)   # device in the upper part, legend below
    elif view == "section_wheel":
        cam.SetParallelScale((bounds[1] - bounds[0]) / 2 / aspect * 1.08)
        cam.SetFocalPoint(cx, cy, cz + 6); cam.SetPosition(cx, cy - 400, cz + 6)
    else:
        cam.Zoom(1.15 if view == "exploded" else 1.45)
    ren.ResetCameraClippingRange(bounds)
    legend = vtk.vtkLegendBoxActor()
    entries = [("frame / shell", (0.62, 0.64, 0.66)), ("datum pins", (0.9, 0.2, 0.2)), ("new PCB", (0.2, 0.3, 0.7)),
               ("donor PCB", (0.15, 0.45, 0.25)), ("tact switch", (0.9, 0.75, 0.2)), ("component", (0.85, 0.35, 0.2)),
               ("battery", (0.95, 0.8, 0.2)), ("haptic", (0.6, 0.2, 0.7)), ("link / flex", (0.9, 0.9, 0.2)),
               ("RF window", (0.2, 0.8, 0.3)), ("removed (ghost)", (0.5, 0.5, 0.9))]
    legend.SetNumberOfEntries(len(entries))
    sq = vtk.vtkCubeSource(); sq.Update()
    for i, (nm, col) in enumerate(entries):
        legend.SetEntry(i, sq.GetOutput(), nm, col)
    legend.GetEntryTextProperty().SetFontSize(14)
    legend.GetEntryTextProperty().SetColor(0.1, 0.1, 0.1)
    legend.UseBackgroundOn(); legend.SetBackgroundColor(1, 1, 1); legend.SetBackgroundOpacity(0.8)
    legend.GetPositionCoordinate().SetCoordinateSystemToNormalizedViewport()
    if view.startswith("section"):
        legend.GetPositionCoordinate().SetValue(0.01, 0.02)
        legend.GetPosition2Coordinate().SetCoordinateSystemToNormalizedViewport()
        legend.GetPosition2Coordinate().SetValue(0.13, 0.30)
    else:
        legend.GetPositionCoordinate().SetValue(0.80, 0.02)
        legend.GetPosition2Coordinate().SetCoordinateSystemToNormalizedViewport()
        legend.GetPosition2Coordinate().SetValue(0.19, 0.30)
    ren.AddViewProp(legend)
    light = vtk.vtkLight()
    light.SetLightTypeToCameraLight()
    light.SetPosition(1, -1, 2)
    light.SetIntensity(0.9)
    ren.AddLight(light)
    txt = vtk.vtkTextActor()
    txt.SetInput(f"neuPod  {layout}  {view}   (depth model: {p.DEPTH_MODEL})")
    txt.GetTextProperty().SetFontSize(22)
    txt.GetTextProperty().SetColor(0.1, 0.1, 0.1)
    txt.SetPosition(12, size[1] - 34)
    ren.AddViewProp(txt)
    rw = vtk.vtkRenderWindow()
    rw.SetOffScreenRendering(1)
    rw.AddRenderer(ren)
    rw.SetSize(*size)
    rw.SetMultiSamples(4)
    rw.Render()
    w2i = vtk.vtkWindowToImageFilter()
    w2i.SetInput(rw)
    w2i.Update()
    pw = vtk.vtkPNGWriter()
    pw.SetFileName(str(out))
    pw.SetInputConnection(w2i.GetOutputPort())
    pw.SetCompressionLevel(9)
    pw.Write()


VIEWS = ("iso", "top", "section_side", "section_wheel", "exploded")

if __name__ == "__main__":
    RENDERS.mkdir(exist_ok=True)
    layouts = sys.argv[1:] or ["stock", "A", "B", "C", "D1", "D2"]
    for lay in layouts:
        for v in VIEWS:
            out = RENDERS / f"{lay}_{v}.png"
            render(lay, v, out)
            print("wrote", out, out.stat().st_size // 1024, "KB")
