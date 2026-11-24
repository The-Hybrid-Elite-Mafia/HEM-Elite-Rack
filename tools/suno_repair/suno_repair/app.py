"""HEM Suno Repair — the Windows program.

Pick the stems folder (and the original mix if you have it), pick the loudness target, press RUN.
Everything is measured, planned by Claude, repaired, mastered and checked; the files land in a new
folder next to the stems. The Claude API key is kept in %APPDATA%\\HEM Suno Repair\\settings.json
(plain text, so it can always be read back).

Run with arguments (same as `python -m suno_repair ...`) it works without the window, for tests.
"""
from __future__ import annotations

import contextlib
import datetime as dt
import io
import json
import os
import queue
import subprocess
import sys
import threading
import traceback
from pathlib import Path

APP_NAME = "HEM Suno Repair"
MODELS = ("claude-sonnet-4-5", "claude-opus-4-1")
TARGETS = {"-14 LUFS (Streaming Standard)": -14.0, "-12 LUFS (Punchy Modern)": -12.0, "-11 LUFS (Country-Trap Max)": -11.0}


def app_dir() -> Path:
    base = Path(os.environ.get("APPDATA", Path.home()))
    d = base / APP_NAME
    d.mkdir(parents=True, exist_ok=True)
    return d


def bundled_dir() -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))


def put_ffmpeg_on_path() -> None:
    """The packaged program ships ffmpeg.exe / ffprobe.exe (for MP3 stems) next to itself."""
    for d in (bundled_dir(), Path(sys.executable).resolve().parent):
        if (d / "ffmpeg.exe").exists():
            os.environ["PATH"] = str(d) + os.pathsep + os.environ.get("PATH", "")
            return


def load_settings() -> dict:
    p = app_dir() / "settings.json"
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}


def save_settings(s: dict) -> None:
    (app_dir() / "settings.json").write_text(json.dumps(s, indent=2), encoding="utf-8")


class _QueueWriter(io.TextIOBase):
    def __init__(self, q: "queue.Queue[str]", log_file):
        self.q, self.f = q, log_file

    def write(self, s: str) -> int:
        if s:
            self.q.put(s)
            self.f.write(s)
            self.f.flush()
        return len(s)


def _dpi_aware() -> None:
    if sys.platform == "win32":
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass


def run_gui() -> int:
    import tkinter as tk
    from tkinter import filedialog, messagebox

    from PIL import ImageTk

    from . import skin
    from .cli import main as pipeline_main

    _dpi_aware()
    st = load_settings()
    root = tk.Tk()
    root.withdraw()
    root.title("HEM Suno Repair · Hybrid Elite Mafia")
    dpi = root.winfo_fpixels("1i") / 96.0
    s = max(0.7, min(dpi, root.winfo_screenwidth() * 0.94 / skin.BASE_W, root.winfo_screenheight() * 0.88 / skin.BASE_H))
    W, H = int(skin.BASE_W * s), int(skin.BASE_H * s)
    root.geometry(f"{W}x{H}")
    root.resizable(False, False)
    root.configure(bg="#0a0a0a")
    try:
        ico = bundled_dir() / "HEMEliteRack.ico"
        if ico.exists():
            root.iconbitmap(str(ico))
    except tk.TclError:
        pass

    def S(v):
        return int(round(v * s))

    def box(b):
        return tuple(S(v) for v in b)

    keep = []          # PhotoImage references (Tk drops images that Python forgets)

    def photo(im):
        p = ImageTk.PhotoImage(im)
        keep.append(p)
        return p

    cv = tk.Canvas(root, width=W, height=H, highlightthickness=0, bd=0, bg="#0a0a0a")
    cv.pack()
    cv.create_image(0, 0, image=photo(skin.render_chrome(s)), anchor="nw")

    ui_font = ("Segoe UI", 12) if sys.platform == "win32" else ("DejaVu Sans", 12)
    mono = ("Consolas", 11) if sys.platform == "win32" else ("DejaVu Sans Mono", 10)

    def entry(b, var, show=None, readonly=False):
        x0, y0, x1, y1 = box(b)
        e = tk.Entry(root, textvariable=var, bg="#0c0c0e", fg="#ece8e0", insertbackground="#d4a04a",
                     disabledbackground="#0c0c0e", readonlybackground="#0c0c0e", relief="flat", bd=0,
                     highlightthickness=0, font=ui_font, show=show or "")
        if readonly:
            e.configure(state="readonly")
        pad = S(10)
        cv.create_window(x0 + pad, (y0 + y1) // 2, window=e, anchor="w", width=x1 - x0 - 2 * pad, height=y1 - y0 - S(12))
        return e

    def image_button(b, render, label, command, kind="metal"):
        x0, y0, x1, y1 = box(b)
        w, h = x1 - x0, y1 - y0
        imgs = {k: photo(render(w, h, label, s, k)) for k in ("normal", "hover", "pressed", "disabled")}
        item = cv.create_image(x0, y0, image=imgs["normal"], anchor="nw")
        state = {"enabled": True, "imgs": imgs, "label": label}

        def set_img(k):
            cv.itemconfigure(item, image=state["imgs"][k])

        def relabel(text):
            state["label"] = text
            state["imgs"] = {k: photo(render(w, h, text, s, k)) for k in ("normal", "hover", "pressed", "disabled")}
            set_img("normal" if state["enabled"] else "disabled")

        def enable(on):
            state["enabled"] = on
            set_img("normal" if on else "disabled")
            cv.itemconfigure(item, state="normal")

        cv.tag_bind(item, "<Enter>", lambda e: state["enabled"] and (set_img("hover"), cv.configure(cursor="hand2")))
        cv.tag_bind(item, "<Leave>", lambda e: (set_img("normal" if state["enabled"] else "disabled"), cv.configure(cursor="")))
        cv.tag_bind(item, "<ButtonPress-1>", lambda e: state["enabled"] and set_img("pressed"))

        def release(e):
            if not state["enabled"]:
                return
            set_img("hover")
            if x0 <= e.x <= x1 and y0 <= e.y <= y1:
                command()
        cv.tag_bind(item, "<ButtonRelease-1>", release)
        return type("Btn", (), {"enable": staticmethod(enable), "relabel": staticmethod(relabel)})

    rows = skin.LAYOUT["rows"]
    stems = tk.StringVar(value="")
    mix = tk.StringVar(value="")
    key = tk.StringVar(value=st.get("api_key", ""))
    model = tk.StringVar(value=st.get("model", MODELS[0]))
    target = tk.StringVar(value=st.get("target", list(TARGETS)[0]) if st.get("target") in TARGETS else list(TARGETS)[0])
    picked: list[str] = []
    state = {"out": None, "running": False, "show_key": False}

    entry(rows["stems"][2], stems, readonly=True)
    entry(rows["mix"][2], mix, readonly=True)
    model_entry = entry(rows["model"][2], model)
    key_entry = entry(rows["key"][2], key, show="•")

    # loudness target: a gold-rimmed field that opens a dark menu
    tb = box(rows["target"][2])
    target_text = cv.create_text(tb[0] + S(14), (tb[1] + tb[3]) // 2, text=target.get(), anchor="w", fill="#ece8e0", font=ui_font)
    menu = tk.Menu(root, tearoff=0, bg="#16140f", fg="#ece8e0", activebackground="#d4a04a", activeforeground="#1a1008",
                   font=ui_font, bd=0, relief="flat")
    for name in TARGETS:
        menu.add_command(label=name, command=lambda n=name: (target.set(n), cv.itemconfigure(target_text, text=n)))
    hit = cv.create_rectangle(*tb, outline="", fill="")
    for it in (hit, target_text):
        cv.tag_bind(it, "<Button-1>", lambda e: menu.tk_popup(root.winfo_rootx() + tb[0], root.winfo_rooty() + tb[3] + S(2)))
        cv.tag_bind(it, "<Enter>", lambda e: cv.configure(cursor="hand2"))
        cv.tag_bind(it, "<Leave>", lambda e: cv.configure(cursor=""))
    cv.tag_raise(target_text)

    # status line + terminal
    stx, sty = skin.LAYOUT["status_text"]
    status_item = cv.create_text(S(stx) - S(20), S(sty), text="Ready for stem batch ingestion.", anchor="w",
                                 fill="#68ec78", font=(ui_font[0], 15))
    t = box(skin.LAYOUT["terminal"])
    logbox = tk.Text(root, bg="#07140b", fg="#6cf07c", insertbackground="#6cf07c", relief="flat", bd=0,
                     highlightthickness=0, font=mono, wrap="word", padx=S(10), pady=S(8))
    sb = tk.Scrollbar(root, command=logbox.yview, bg="#0c1a10", troughcolor="#07140b", relief="flat", bd=0, width=S(12))
    logbox.configure(yscrollcommand=sb.set)
    cv.create_window(t[0] + S(3), t[1] + S(3), window=logbox, anchor="nw", width=t[2] - t[0] - S(20), height=t[3] - t[1] - S(6))
    cv.create_window(t[2] - S(16), t[1] + S(3), window=sb, anchor="nw", width=S(13), height=t[3] - t[1] - S(6))

    def log(line: str):
        logbox.insert("end", line)
        logbox.see("end")

    def set_status(text, color="#68ec78"):
        cv.itemconfigure(status_item, text=text, fill=color)

    log("> [Init] Pick your stems (Files / Zip) and press EXECUTE.\n")

    # ---- actions
    def show_picked(paths):
        from .audio_io import resolve_inputs
        try:
            files = resolve_inputs(paths)
        except SystemExit as e:
            messagebox.showerror(APP_NAME, str(e))
            return
        if not files:
            messagebox.showerror(APP_NAME, "No audio files (WAV, MP3, FLAC, AIFF) found there.")
            return
        picked[:] = [str(p) for p in paths]
        stems.set("  |  ".join(Path(p).name for p in paths))
        logbox.delete("1.0", "end")
        log(f"> [Init] Scanning stems... {len(files)} audio files found\n")
        for f in files:
            log(f">   Loaded: {f.name}\n")
        set_status(f"{len(files)} stems ready. Press EXECUTE REPAIR ENGINE.")

    def pick_stems():
        fs = filedialog.askopenfilenames(title="Select your stems (WAV / MP3), or the Suno .zip",
                                         initialdir=st.get("last_dir") or None,
                                         filetypes=[("Stems or Suno zip", "*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.zip"), ("All files", "*.*")])
        if fs:
            show_picked(list(fs))

    def pick_folder():
        d = filedialog.askdirectory(title="Folder that holds your stems (this box shows folders only)",
                                    initialdir=st.get("last_dir") or None)
        if d:
            show_picked([d])

    def pick_mix():
        f = filedialog.askopenfilename(title="Original stereo mix (optional)",
                                       filetypes=[("Audio", "*.wav *.flac *.mp3 *.aif *.aiff *.m4a"), ("All", "*.*")])
        mix.set(f or "")

    def toggle_key():
        state["show_key"] = not state["show_key"]
        key_entry.configure(show="" if state["show_key"] else "•")
        cv.itemconfigure(show_item, image=show_imgs[state["show_key"]])

    sbx = box(rows["stems"][3])
    half = (sbx[2] - sbx[0] - S(8)) // 2
    image_button((rows["stems"][3][0], rows["stems"][3][1], rows["stems"][3][0] + half / s, rows["stems"][3][3]), skin.metal_button, "Files / Zip…", pick_stems)
    image_button((rows["stems"][3][0] + half / s + 8, rows["stems"][3][1], rows["stems"][3][2], rows["stems"][3][3]), skin.metal_button, "Folder…", pick_folder)
    image_button(rows["mix"][3], skin.metal_button, "Browse…", pick_mix)
    kb = box(rows["key"][3])
    show_imgs = {v: photo(skin.toggle(kb[2] - kb[0], kb[3] - kb[1], "Show", v, s)) for v in (False, True)}
    show_item = cv.create_image(kb[0], kb[1], image=show_imgs[False], anchor="nw")
    cv.tag_bind(show_item, "<Button-1>", lambda e: toggle_key())

    q: "queue.Queue[str]" = queue.Queue()

    def pump():
        try:
            while True:
                m = q.get_nowait()
                if m.startswith("\x00DONE"):
                    finish(m)
                else:
                    for line in m.splitlines(keepends=True):
                        log(("> " + line) if line.strip() and not line.startswith(" ") else line)
        except queue.Empty:
            pass
        root.after(150, pump)

    def finish(msg: str):
        code = int(msg.split(":", 1)[1])
        state["running"] = False
        run_btn.enable(True)
        run_btn.relabel("EXECUTE REPAIR ENGINE")
        out = state["out"]
        if out and out.exists():
            open_btn.enable(True)
        if code == 0:
            set_status("DONE — all checks pass. Master exported.")
            log(f"> [Success] Master exported to: {out}\n")
        elif code == 5:
            set_status("DONE — some checks FAILED (see verification.md).", "#f0c060")
        else:
            set_status(f"STOPPED with an error (code {code}) — see the log.", "#ff6b5b")

    def worker(argv: list[str], out: Path):
        out.mkdir(parents=True, exist_ok=True)
        with open(out / "run_log.txt", "w", encoding="utf-8") as lf:
            w = _QueueWriter(q, lf)
            code = 1
            with contextlib.redirect_stdout(w), contextlib.redirect_stderr(w):
                try:
                    code = pipeline_main(argv)
                except SystemExit as e:
                    code = e.code if isinstance(e.code, int) else 1
                    if not isinstance(e.code, int) and e.code:
                        print(str(e.code))
                except Exception:
                    traceback.print_exc()
                    code = 1
        q.put(f"\x00DONE:{code}")

    def start():
        if state["running"]:
            return
        if not picked:
            messagebox.showerror(APP_NAME, "Pick your stems first (Files / Zip… or Folder…).")
            return
        from .audio_io import resolve_inputs
        files = resolve_inputs(picked)
        if not files:
            messagebox.showerror(APP_NAME, "No audio files found in what you picked.")
            return
        folder = files[0].parent
        k = key.get().strip()
        save_settings({"last_dir": str(folder), "target": target.get(), "model": model.get().strip(), "api_key": k})
        stamp = dt.datetime.now().strftime("%Y-%m-%d %H-%M")
        out = folder / f"HEM Repair {stamp}"
        argv = [str(p) for p in picked] + ["--out", str(out), "--target", str(TARGETS[target.get()])]
        if mix.get().strip():
            argv += ["--mix", mix.get().strip()]
        if k:
            os.environ["ANTHROPIC_API_KEY"] = k
            argv += ["--planner", "claude", "--model", model.get().strip() or MODELS[0]]
        else:
            if not messagebox.askyesno(APP_NAME, "No Claude API key is set, so Claude cannot plan the repairs.\n\n"
                                                 "Run with the built-in rules instead?"):
                return
            argv += ["--planner", "rules"]
        logbox.delete("1.0", "end")
        log("> [DSP] Measuring stems, Claude plan, repair, master, verification...\n")
        state.update(out=out, running=True)
        open_btn.enable(False)
        run_btn.enable(False)
        run_btn.relabel("WORKING…")
        set_status("Working… a full song takes several minutes.", "#f0c060")
        threading.Thread(target=worker, args=(argv, out), daemon=True).start()

    def open_out():
        out = state["out"]
        if out and out.exists():
            if hasattr(os, "startfile"):
                os.startfile(str(out))
            else:
                subprocess.Popen(["xdg-open", str(out)])

    run_btn = image_button(skin.LAYOUT["execute"], skin.gold_bar, "EXECUTE REPAIR ENGINE", start)
    open_btn = image_button(skin.LAYOUT["open_btn"], skin.metal_button, "Open output folder", open_out)
    open_btn.enable(False)

    if os.environ.get("HEM_PRESELECT_STEMS"):      # for scripted tests: start with these stems picked
        root.after(300, lambda: show_picked(os.environ["HEM_PRESELECT_STEMS"].split(os.pathsep)))
    root.after(150, pump)
    root.deiconify()
    root.mainloop()
    return 0


def main() -> int:
    put_ffmpeg_on_path()
    if len(sys.argv) > 1:          # command-line mode (tests, scripts)
        from .cli import main as pipeline_main
        if "ANTHROPIC_API_KEY" not in os.environ and load_settings().get("api_key"):
            os.environ["ANTHROPIC_API_KEY"] = load_settings()["api_key"]
        return pipeline_main(sys.argv[1:])
    return run_gui()
