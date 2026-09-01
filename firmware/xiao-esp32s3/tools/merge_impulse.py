#!/usr/bin/env python3
"""
Merge a second Edge Impulse project into lib/epod_inferencing.

Two impulses trained in two separate Edge Impulse projects cannot be exported as
one library without an Enterprise plan, so this does the same job by hand. It
works because every symbol Edge Impulse generates is already namespaced by
project id, and because the impulse struct carries literal values rather than
the EI_CLASSIFIER_* macros - so model_metadata.h, the one file that genuinely
collides, is not needed from the second export at all.

    python tools/merge_impulse.py "path/to/second-export"

What it does:
  1. copies the second model's tflite-model/*_compiled.{cpp,h}
  2. rewrites trained_model_ops_define.h as the INTERSECTION of both DISABLE
     sets - a kernel is only compiled out when NEITHER model uses it
  3. appends the second impulse's block to model_variables.h, keeping exactly
     one ei_default_impulse (the voice one)

Afterwards, run the second impulse explicitly:

    ei_impulse_result_t r;
    run_classifier(&impulse_handle_<projectid>_1, &signal, &r, false);

CHECK THE SIZE FIRST. A model only fits if it was built for microcontrollers:
FOMO for object detection, not MobileNet SSD. If the export's model_metadata.h
says EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE is 0, Edge Impulse is telling you
it will not run under TensorFlow Lite Micro at all, and no merge will change
that.
"""

import io
import os
import re
import shutil
import sys

LIB = os.path.join("lib", "epod_inferencing", "src")


def die(msg):
    sys.exit("merge_impulse: " + msg)


def disables(path):
    out = {}
    for ln in io.open(path, encoding="utf-8"):
        m = re.match(r"\s*#define\s+(EI_TFLITE_DISABLE_\w+)\s+1", ln)
        if m:
            out[m.group(1)] = ln.rstrip("\n")
    return out


def main():
    if len(sys.argv) != 2:
        die("usage: python tools/merge_impulse.py <path-to-export>")
    src = sys.argv[1]
    if not os.path.isdir(src):
        die("no such folder: " + src)

    # Some exports nest everything one level down.
    if not os.path.isdir(os.path.join(src, "model-parameters")):
        inner = os.path.join(src, os.path.basename(src))
        for c in os.listdir(src):
            if os.path.isdir(os.path.join(src, c, "model-parameters")):
                inner = os.path.join(src, c)
                break
        src = inner
    mp = os.path.join(src, "model-parameters")
    if not os.path.isdir(mp):
        die("no model-parameters/ under " + src)

    # ---- refuse models that cannot run on an MCU ---------------------------
    meta = io.open(os.path.join(mp, "model_metadata.h"), encoding="utf-8").read()
    m = re.search(r"EI_CLASSIFIER_TFLITE_LARGEST_ARENA_SIZE\s+(\d+)", meta)
    if m and int(m.group(1)) == 0:
        die("this export sets LARGEST_ARENA_SIZE 0 - Edge Impulse says it cannot\n"
            "  run under TensorFlow Lite Micro. Re-train as FOMO for MCU targets.")

    pid = re.search(r"ei_classifier_inferencing_categories_(\d+)_1",
                    io.open(os.path.join(mp, "model_variables.h"), encoding="utf-8").read())
    if not pid:
        die("could not find the project id in model_variables.h")
    pid = pid.group(1)
    print("  project id      %s" % pid)

    # ---- 1. compiled model --------------------------------------------------
    tm = os.path.join(src, "tflite-model")
    copied = []
    for f in os.listdir(tm):
        if f.startswith("tflite_learn_%s" % pid):
            shutil.copy(os.path.join(tm, f), os.path.join(LIB, "tflite-model", f))
            copied.append(f)
    if not copied:
        die("no tflite_learn_%s* files in %s" % (pid, tm))
    mb = sum(os.path.getsize(os.path.join(LIB, "tflite-model", f)) for f in copied) / 1048576.0
    print("  model source    %.1f MB  (%s)" % (mb, ", ".join(copied)))
    if mb > 4:
        print("  WARNING: that is very large. The S3 app partition is ~3.3 MB of flash.")

    # ---- 2. ops define: intersection ---------------------------------------
    ov = os.path.join(LIB, "tflite-model", "trained_model_ops_define.h")
    dv, dp = disables(ov), disables(os.path.join(tm, "trained_model_ops_define.h"))
    both = sorted(set(dv) & set(dp))
    io.open(ov, "w", encoding="utf-8", newline="").write(
        "#ifndef EI_TFLITE_MODEL_OPS_DEFINES_H\n#define EI_TFLITE_MODEL_OPS_DEFINES_H\n\n"
        "// MERGED by tools/merge_impulse.py.\n"
        "// A DISABLE is kept only when BOTH models disable that kernel. Taking the\n"
        "// union instead compiles out a kernel the other model needs, and that\n"
        "// shows up at runtime as a model which loads and then returns nothing.\n\n"
        + "\n".join(dv[k] for k in both)
        + "\n\n#endif // EI_TFLITE_MODEL_OPS_DEFINES_H\n")
    print("  kernels         %d disabled, %d re-enabled for the new model"
          % (len(both), len(set(dv) | set(dp)) - len(both)))

    # ---- 3. model_variables.h ----------------------------------------------
    pv = io.open(os.path.join(mp, "model_variables.h"), encoding="utf-8").read()
    start = pv.index("\n", pv.rindex("#include")) + 1
    body = pv[start:pv.index("ei_impulse_handle_t& ei_default_impulse")].rstrip()

    vp = os.path.join(LIB, "model-parameters", "model_variables.h")
    vs = io.open(vp, encoding="utf-8").read()
    if ("_%s_" % pid) in vs:
        die("project %s already merged - revert before re-running" % pid)

    hdr = [f for f in copied if f.endswith(".h")]
    if hdr:
        anchor_i = '#include "tflite-model/'
        first = vs.index(anchor_i)
        eol = vs.index("\n", first) + 1
        vs = vs[:eol] + '#include "tflite-model/%s"\n' % hdr[0] + vs[eol:]

    banner = (
        "\n\n// ===========================================================================\n"
        "//  SECOND IMPULSE: project %s, merged by tools/merge_impulse.py\n"
        "//  ei_default_impulse stays bound to the VOICE impulse below; run this one\n"
        "//  explicitly through impulse_handle_%s_1.\n"
        "// ===========================================================================\n\n" % (pid, pid))
    vs = vs.replace("ei_impulse_handle_t& ei_default_impulse",
                    banner + body + "\n\n" + "ei_impulse_handle_t& ei_default_impulse", 1)
    io.open(vp, "w", encoding="utf-8", newline="").write(vs)

    print("  merged          impulse_handle_%s_1 is now available" % pid)
    print("\n  Build it. If the link fails on a Register_* symbol, the two exports were\n"
          "  generated against different TFLite generations - add a forwarder beside\n"
          "  the one in lib/epod_inferencing/src/tflite-model/ei_merge_shim.cpp.")


if __name__ == "__main__":
    main()
