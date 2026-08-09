#!/usr/bin/env python3
"""GTK3 self-triggering text sink for ibus-engine-probe (ADR-0002 gate).

The moment its TextView gains keyboard focus, it runs ./ibus-probe in a thread
(so the sink keeps focus while the engine switches + commits). The committed
UTF-8 lands in this TextView; the buffer is mirrored to OUT on every change and
again after the probe finishes. Touches RAN when the probe has executed.

Run with GTK_IM_MODULE=ibus so the TextView uses an IBus input context.
"""
import os, subprocess, threading
import gi
gi.require_version("Gtk", "3.0")
gi.require_version("GLib", "2.0")
from gi.repository import Gtk, GLib

OUT = "/tmp/ibus_out.txt"
PROBE_ERR = "/tmp/probe.err"
RAN = "/tmp/probe_ran"


def main():
    win = Gtk.Window(title="ibus-probe target — focused")
    win.set_default_size(560, 240)
    win.connect("destroy", lambda *_: Gtk.main_quit())
    view = Gtk.TextView()
    view.set_editable(True)
    view.set_vexpand(True)
    buf = view.get_buffer()
    win.add(view)

    def buffer_text():
        return buf.get_text(buf.get_start_iter(), buf.get_end_iter(), True)

    def dump(_=None):
        with open(OUT, "w") as f:
            f.write(buffer_text())
        return False

    buf.connect("changed", dump)

    ran = {"v": False}

    def run_probe():
        r = subprocess.run(["./ibus-probe"], capture_output=True, text=True)
        with open(PROBE_ERR, "w") as f:
            f.write(r.stderr)
        open(RAN, "w").close()
        GLib.idle_add(dump)
        GLib.timeout_add(400, lambda: (Gtk.main_quit(), False)[1])

    def on_focus_in(_widget, _event):
        if ran["v"]:
            return False
        ran["v"] = True
        threading.Thread(target=run_probe, daemon=True).start()
        return False

    view.connect("focus-in-event", on_focus_in)

    for p in (OUT, PROBE_ERR, RAN):
        try:
            os.unlink(p)
        except FileNotFoundError:
            pass

    win.show_all()
    win.present()
    GLib.idle_add(lambda: (view.grab_focus(), False)[1])
    GLib.timeout_add_seconds(30, lambda: (Gtk.main_quit(), False)[1])
    Gtk.main()


if __name__ == "__main__":
    main()
