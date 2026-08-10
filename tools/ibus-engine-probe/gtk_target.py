#!/usr/bin/env python3
"""Passive GTK3 text target for OpenGlide IBus commit verification.

Focuses an editable TextView on launch and mirrors its buffer to OUT on every
change — the readback for og_ibus_commit. Run with GTK_IM_MODULE=ibus so the
TextView uses an IBus input context (the prototype's self-activated engine)."""
import os
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, GLib

OUT = "/tmp/og_ibus_out.txt"


def main():
    win = Gtk.Window(title="OpenGlide IBus target — focused")
    win.set_default_size(480, 200)
    win.connect("destroy", lambda *_: Gtk.main_quit())
    view = Gtk.TextView()
    view.set_editable(True)
    view.set_vexpand(True)
    buf = view.get_buffer()
    win.add(view)

    def dump(_=None):
        with open(OUT, "w") as f:
            f.write(buf.get_text(buf.get_start_iter(), buf.get_end_iter(), True))
        return False

    buf.connect("changed", dump)
    try:
        os.unlink(OUT)
    except FileNotFoundError:
        pass

    win.show_all()
    win.present()
    GLib.idle_add(lambda: (view.grab_focus(), False)[1])
    GLib.timeout_add_seconds(25, lambda: (Gtk.main_quit(), False)[1])
    Gtk.main()


if __name__ == "__main__":
    main()
