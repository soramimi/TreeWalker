#!/usr/bin/env python3
"""Thunar integration. Run on an isolated X11 display under dbus-run-session.
Requires thunar, xdotool and the built TransferTests executable.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument("--runner", required=True)
parser.add_argument("--drag", action="store_true")
args = parser.parse_args()
runner = str(Path(args.runner).resolve())


def run(*command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()


def wait_for(predicate, description, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("Timed out: " + description)


def windows(name):
    result = subprocess.run(["xdotool", "search", "--onlyvisible", "--name", name], text=True, capture_output=True)
    return result.stdout.splitlines() if result.returncode == 0 else []


with tempfile.TemporaryDirectory(prefix="treewalker-native-") as temporary:
    root = Path(temporary)
    os.environ["XDG_CONFIG_HOME"] = str(root / "config")
    os.environ["XDG_CACHE_HOME"] = str(root / "cache")
    os.environ["XDG_DATA_HOME"] = str(root / "data")
    for name in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME"):
        Path(os.environ[name]).mkdir()
    subprocess.run(["dbus-update-activation-environment", "XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_DATA_HOME", "DISPLAY"], check=True)
    os.environ.pop("QT_QPA_PLATFORM", None)
    log = (root / "thunar.log").open("w")
    source = root / "source"
    source.mkdir()
    target = root / "target"
    target.mkdir()
    file = source / "payload.txt"
    file.write_text("native-transfer-payload")
    thunar = subprocess.Popen(["thunar", str(source)], stdout=log, stderr=log)
    helpers = []
    try:
        window = wait_for(lambda: windows("source - Thunar"), "Thunar source window")[0]
        run("xdotool", "windowmove", window, "0", "0", "windowsize", window, "600", "700")
        run("xdotool", "windowfocus", "--sync", window, "key", "ctrl+2")
        time.sleep(0.3)
        for operation in ("copy", "move"):
            file.write_text("native-transfer-payload")
            output = target / operation
            output.mkdir()
            run("xdotool", "windowfocus", "--sync", window, "key", "ctrl+a", "ctrl+" + ("c" if operation == "copy" else "x"))
            time.sleep(0.2)
            subprocess.run([runner, "--clipboard-probe", str(file), operation, str(output)], check=True, timeout=10)
            assert (output / file.name).read_text() == "native-transfer-payload"
            assert file.exists() == (operation == "copy")
            print("PASS Thunar -> TreeWalker clipboard", operation, flush=True)
        for operation in ("copy", "move"):
            file.write_text("native-transfer-payload")
            output = target / ("reverse-" + operation)
            output.mkdir()
            publisher = subprocess.Popen([runner, "--clipboard-publish", str(file), operation], stdout=log, stderr=log)
            helpers.append(publisher)
            time.sleep(0.3)
            run("xdotool", "windowfocus", "--sync", window, "key", "ctrl+l")
            run("xdotool", "type", "--clearmodifiers", str(output))
            run("xdotool", "key", "Return")
            time.sleep(0.3)
            run("xdotool", "key", "ctrl+v")
            wait_for(lambda: (output / file.name).exists(), "Thunar paste")
            assert (output / file.name).read_text() == "native-transfer-payload"
            wait_for(lambda: file.exists() == (operation == "copy"), "source state")
            publisher.terminate()
            publisher.wait(timeout=5)
            print("PASS TreeWalker -> Thunar clipboard", operation, flush=True)
        if args.drag:
            for choice, modifier in (("Copy", "ctrl"), ("Move", ""), ("Cancel", "shift")):
                file.write_text("native-transfer-payload")
                output = target / ("drag-" + choice)
                output.mkdir()
                run("xdotool", "windowfocus", "--sync", window, "key", "ctrl+l")
                run("xdotool", "type", "--clearmodifiers", str(source))
                run("xdotool", "key", "Return")
                time.sleep(0.3)
                receiver = subprocess.Popen([runner, "--drop-receiver", str(file), str(output), choice], stdout=log, stderr=log)
                helpers.append(receiver)
                wait_for(lambda: windows("TreeWalker Transfer Receiver"), "drop receiver")
                run("xdotool", "windowfocus", "--sync", window, "key", "ctrl+a")
                run("xdotool", "mousemove", "--sync", "235", "110")
                run("xdotool", "mousedown", "1")
                time.sleep(0.2)
                run("xdotool", "mousemove", "--sync", "255", "110")
                time.sleep(0.2)
                if modifier:
                    run("xdotool", "keydown", modifier)
                for x, y in ((350, 200), (610, 200), (850, 200)):
                    run("xdotool", "mousemove", "--sync", str(x), str(y))
                    time.sleep(0.15)
                run("xdotool", "mouseup", "1")
                if modifier:
                    run("xdotool", "keyup", modifier)
                code = receiver.wait(timeout=20)
                assert code == 0, "drop receiver exit " + str(code)
                assert (output / file.name).exists() == (choice != "Cancel")
                assert file.exists() == (choice != "Move")
                print("PASS Thunar -> TreeWalker drag", choice, modifier or "no modifier", flush=True)
    finally:
        for helper in helpers:
            if helper.poll() is None:
                helper.terminate()
                helper.wait(timeout=5)
        subprocess.run(["thunar", "--quit"], stdout=log, stderr=log, timeout=5)
        if thunar.poll() is None:
            thunar.terminate()
        thunar.wait(timeout=5)
        log.close()
        # Retain only diagnostic text from our isolated test session on failure.
        if os.environ.get("TREEWALKER_NATIVE_LOG"):
            Path(os.environ["TREEWALKER_NATIVE_LOG"]).write_text((root / "thunar.log").read_text())
