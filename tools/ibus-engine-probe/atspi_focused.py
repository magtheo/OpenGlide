#!/usr/bin/env python3
"""AT-SPI focus inspector — print the currently-focused accessible: role, name,
and text. Used to (a) confirm a target widget holds keyboard focus before an IBus
commit, and (b) read back committed text from any a11y-exposing app."""
import sys
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi

def find_focused(o, depth=0, maxd=7):
    if o is None:
        return None
    try:
        ss = o.get_state_set()
        if ss and ss.contains(Atspi.STATE_FOCUSED):
            return o
    except Exception:
        pass
    if depth >= maxd:
        return None
    try:
        n = o.get_child_count()
    except Exception:
        n = 0
    for i in range(n):
        try:
            c = o.get_child_at_index(i)
        except Exception:
            c = None
        r = find_focused(c, depth + 1, maxd)
        if r is not None:
            return r
    return None

def main():
    desk = Atspi.get_desktop(0)
    foc = None
    for i in range(desk.get_child_count()):
        app = desk.get_child_at_index(i)
        foc = find_focused(app)
        if foc is not None:
            break
    if foc is None:
        print("NO FOCUSED ACCESSIBLE FOUND")
        return
    try:
        role = foc.get_role_name()
    except Exception:
        role = "?"
    try:
        name = foc.get_name()
    except Exception:
        name = "?"
    txt = ""
    try:
        t = foc.query_text()
        txt = t.getText(0, t.characterCount)
    except Exception as e:
        txt = "(no Atspi.Text: %s)" % e
    appname = ""
    try:
        appname = foc.get_application().get_name()
    except Exception:
        pass
    print("APP=%r ROLE=%r NAME=%r" % (appname, role, name))
    print("TEXT=%r" % (txt,))

if __name__ == "__main__":
    main()
