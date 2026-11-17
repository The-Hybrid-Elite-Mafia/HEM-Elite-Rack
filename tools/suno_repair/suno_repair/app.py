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
TARGETS = {"-14 LUFS (streaming)": -14.0, "-11 LUFS (heavy country-trap)": -11.0}


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


def run_gui() -> int:
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    from .cli import main as pipeline_main

    st = load_settings()
    root = tk.Tk()
    root.title(APP_NAME)
    root.geometry("900x680")
    root.configure(bg="#111111")
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass
    for w in ("TFrame", "TLabel", "TCheckbutton"):
        style.configure(w, background="#111111", foreground="#e8e8e8")
    style.configure("TButton", padding=6)
    style.configure("Run.TButton", font=("Segoe UI", 12, "bold"), padding=10)
    style.configure("Title.TLabel", font=("Segoe UI", 16, "bold"), foreground="#d4af37", background="#111111")

    stems = tk.StringVar(value=st.get("last_stems", ""))
    mix = tk.StringVar(value="")
    target = tk.StringVar(value=st.get("target", list(TARGETS)[0]))
    model = tk.StringVar(value=st.get("model", MODELS[0]))
    key = tk.StringVar(value=st.get("api_key", ""))
    show_key = tk.BooleanVar(value=False)
    status = tk.StringVar(value="Pick your stems folder, then press RUN.")

    frm = ttk.Frame(root, padding=14)
    frm.pack(fill="both", expand=True)
    ttk.Label(frm, text="HYBRID ELITE MAFIA  ·  SUNO REPAIR", style="Title.TLabel").grid(row=0, column=0, columnspan=3, sticky="w", pady=(0, 10))

    def pick_stems():
        d = filedialog.askdirectory(title="Folder with your Suno stems", initialdir=stems.get() or None)
        if d:
            stems.set(d)

    def pick_mix():
        f = filedialog.askopenfilename(title="Original stereo mix (optional)",
                                       filetypes=[("Audio", "*.wav *.flac *.mp3 *.aif *.aiff *.m4a"), ("All", "*.*")])
        if f:
            mix.set(f)

    ttk.Label(frm, text="Stems folder").grid(row=1, column=0, sticky="w")
    ttk.Entry(frm, textvariable=stems, width=80).grid(row=1, column=1, sticky="we", padx=6)
    ttk.Button(frm, text="Browse…", command=pick_stems).grid(row=1, column=2)
    ttk.Label(frm, text="Original mix (optional)").grid(row=2, column=0, sticky="w")
    ttk.Entry(frm, textvariable=mix, width=80).grid(row=2, column=1, sticky="we", padx=6)
    mixbtns = ttk.Frame(frm)
    mixbtns.grid(row=2, column=2)
    ttk.Button(mixbtns, text="Browse…", command=pick_mix).pack(side="left")
    ttk.Button(mixbtns, text="Clear", command=lambda: mix.set("")).pack(side="left")
    ttk.Label(frm, text="Loudness target").grid(row=3, column=0, sticky="w", pady=4)
    ttk.Combobox(frm, textvariable=target, values=list(TARGETS), state="readonly", width=34).grid(row=3, column=1, sticky="w", padx=6)
    ttk.Label(frm, text="Claude model").grid(row=4, column=0, sticky="w", pady=4)
    ttk.Combobox(frm, textvariable=model, values=list(MODELS), width=34).grid(row=4, column=1, sticky="w", padx=6)
    ttk.Label(frm, text="Claude API key").grid(row=5, column=0, sticky="w", pady=4)
    key_entry = ttk.Entry(frm, textvariable=key, width=80, show="•")
    key_entry.grid(row=5, column=1, sticky="we", padx=6)
    ttk.Checkbutton(frm, text="Show", variable=show_key,
                    command=lambda: key_entry.configure(show="" if show_key.get() else "•")).grid(row=5, column=2, sticky="w")

    run_btn = ttk.Button(frm, text="RUN", style="Run.TButton")
    run_btn.grid(row=6, column=0, columnspan=3, sticky="we", pady=10)
    ttk.Label(frm, textvariable=status).grid(row=7, column=0, columnspan=3, sticky="w")
    bar = ttk.Progressbar(frm, mode="indeterminate")
    bar.grid(row=8, column=0, columnspan=3, sticky="we", pady=4)

    logbox = tk.Text(frm, height=20, bg="#1b1b1b", fg="#e8e8e8", insertbackground="#e8e8e8", font=("Consolas", 9), wrap="word")
    logbox.grid(row=9, column=0, columnspan=3, sticky="nsew")
    out_btn = ttk.Button(frm, text="Open output folder", state="disabled")
    out_btn.grid(row=10, column=0, columnspan=3, sticky="e", pady=(8, 0))
    frm.columnconfigure(1, weight=1)
    frm.rowconfigure(9, weight=1)

    q: "queue.Queue[str]" = queue.Queue()
    state = {"out": None, "running": False}

    def pump():
        try:
            while True:
                s = q.get_nowait()
                if s.startswith("\x00DONE"):
                    finish(s)
                else:
                    logbox.insert("end", s)
                    logbox.see("end")
        except queue.Empty:
            pass
        root.after(150, pump)

    def finish(msg: str):
        code = int(msg.split(":", 1)[1])
        state["running"] = False
        bar.stop()
        run_btn.configure(state="normal")
        out = state["out"]
        if out and out.exists():
            out_btn.configure(state="normal", command=lambda: os.startfile(str(out)) if hasattr(os, "startfile") else None)
        if code == 0:
            status.set("DONE — all checks pass. Files are in the output folder.")
        elif code == 5:
            status.set("DONE — some checks FAILED (see verification.md). Files are still written.")
        else:
            status.set(f"STOPPED with an error (code {code}). The log above says why.")

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
        folder = Path(stems.get().strip().strip('"'))
        if not folder.is_dir():
            messagebox.showerror(APP_NAME, "Pick the folder that holds your stems first.")
            return
        k = key.get().strip()
        settings = {"last_stems": str(folder), "target": target.get(), "model": model.get().strip(), "api_key": k}
        save_settings(settings)
        stamp = dt.datetime.now().strftime("%Y-%m-%d %H-%M")
        out = folder / f"HEM Repair {stamp}"
        argv = [str(folder), "--out", str(out), "--target", str(TARGETS[target.get()])]
        if mix.get().strip():
            argv += ["--mix", mix.get().strip().strip('"')]
        if k:
            os.environ["ANTHROPIC_API_KEY"] = k
            argv += ["--planner", "claude", "--model", model.get().strip() or MODELS[0]]
        else:
            if not messagebox.askyesno(APP_NAME, "No Claude API key is set, so Claude cannot plan the repairs.\n\n"
                                                 "Run with the built-in rules instead?"):
                return
            argv += ["--planner", "rules"]
        logbox.delete("1.0", "end")
        state.update(out=out, running=True)
        out_btn.configure(state="disabled")
        run_btn.configure(state="disabled")
        status.set("Working… measuring takes a few minutes for a full song.")
        bar.start(12)
        threading.Thread(target=worker, args=(argv, out), daemon=True).start()

    run_btn.configure(command=start)
    root.after(150, pump)
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
