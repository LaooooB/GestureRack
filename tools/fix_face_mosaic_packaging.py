from pathlib import Path

root = Path(__file__).resolve().parents[1]

spec_path = root / "VisionEngine" / "GestureVisionEngine.spec"
spec = spec_path.read_text(encoding="utf-8")
spec = spec.replace("from pathlib import Path\n\nimport cv2\n", "")
spec = spec.replace(
    "(str(Path(cv2.data.haarcascades) / 'haarcascade_frontalface_default.xml'), 'models'),",
    "('models/haarcascade_frontalface_default.xml', 'models'),",
)
if "cv2.data.haarcascades" in spec:
    raise RuntimeError("OpenCV wheel cascade path still present in spec")
if "models/haarcascade_frontalface_default.xml" not in spec:
    raise RuntimeError("Pinned local cascade path missing from spec")
spec_path.write_text(spec, encoding="utf-8", newline="\n")

workflow_path = root / ".github" / "workflows" / "windows-vst3-build.yml"
workflow = workflow_path.read_text(encoding="utf-8")
if "Download bundled OpenCV face cascade" not in workflow:
    needle = """      - name: Download bundled MediaPipe gesture model
        working-directory: VisionEngine
        run: python -c \"from pathlib import Path; from vision_engine import ensure_model; ensure_model(Path('models/gesture_recognizer.task'))\"
"""
    addition = needle + """
      - name: Download bundled OpenCV face cascade
        working-directory: VisionEngine
        run: python -c \"from pathlib import Path; import urllib.request; p=Path('models/haarcascade_frontalface_default.xml'); p.parent.mkdir(parents=True, exist_ok=True); urllib.request.urlretrieve('https://raw.githubusercontent.com/opencv/opencv/4.10.0/data/haarcascades/haarcascade_frontalface_default.xml', p); assert p.stat().st_size > 100000\"
"""
    if needle not in workflow:
        raise RuntimeError("Official workflow MediaPipe insertion point missing")
    workflow = workflow.replace(needle, addition, 1)
workflow_path.write_text(workflow, encoding="utf-8", newline="\n")

print("Pinned OpenCV face cascade packaging normalized.")
