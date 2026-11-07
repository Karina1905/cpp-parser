#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
EXAMPLES = ROOT / "examples"
BIN = ROOT / "cppparser"
TIMEOUT = 5
MAX_OUT = 100_000

MIME = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json; charset=utf-8",
}


def run(cmd, cwd, stdin=""):
    try:
        p = subprocess.run(cmd, cwd=cwd, input=stdin.encode(), capture_output=True, timeout=TIMEOUT)
        return {"out": p.stdout.decode("utf-8", "replace")[:MAX_OUT],
                "err": p.stderr.decode("utf-8", "replace")[:MAX_OUT],
                "exit": p.returncode, "timeout": False}
    except subprocess.TimeoutExpired as e:
        return {"out": (e.stdout or b"").decode("utf-8", "replace")[:MAX_OUT],
                "err": (e.stderr or b"").decode("utf-8", "replace")[:MAX_OUT],
                "exit": -1, "timeout": True}


def compile_and_run(code, stdin):
    if not BIN.exists():
        return {"error": "cppparser не зібрано — виконайте make"}
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        src = tmp / "program.cpp"
        src.write_text(code, encoding="utf-8")
        ast_path, py_path = tmp / "ast.json", tmp / "out.py"

        parse = run([str(BIN), "program.cpp", "--ast", "ast.json", "--gen", "out.py"], tmp)
        result = {"parse_ok": parse["exit"] not in (1, 2), "sema_ok": parse["exit"] == 0,
                  "messages": parse["err"], "ast": None, "python": None, "interp": None, "py": None}
        if ast_path.exists():
            result["ast"] = json.loads(ast_path.read_text(encoding="utf-8"))
        if parse["exit"] != 0:
            return result
        if py_path.exists():
            result["python"] = py_path.read_text(encoding="utf-8")
        result["interp"] = run([str(BIN), "program.cpp", "--run", "--no-sema"], tmp, stdin)
        if result["python"] is not None:
            result["py"] = run([sys.executable, "out.py"], tmp, stdin)
        return result


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write("%s %s\n" % (self.command, self.path))

    def send(self, status, body, ctype="application/json; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/examples":
            items = []
            for f in sorted(EXAMPLES.glob("*.cpp")):
                stdin_file = f.with_suffix(".in")
                items.append({"name": f.stem, "code": f.read_text(encoding="utf-8"),
                              "stdin": stdin_file.read_text(encoding="utf-8") if stdin_file.exists() else ""})
            return self.send(200, json.dumps(items))
        if path == "/":
            path = "/index.html"
        file = (WEB / path.lstrip("/")).resolve()
        if WEB not in file.parents or not file.is_file():
            return self.send(404, "not found", "text/plain; charset=utf-8")
        return self.send(200, file.read_bytes(), MIME.get(file.suffix, "application/octet-stream"))

    def do_POST(self):
        if self.path != "/api/run":
            return self.send(404, "not found", "text/plain; charset=utf-8")
        length = int(self.headers.get("Content-Length", "0"))
        try:
            data = json.loads(self.rfile.read(length) or b"{}")
            result = compile_and_run(data.get("code", ""), data.get("stdin", ""))
        except Exception as e:
            result = {"error": str(e)}
        return self.send(200, json.dumps(result))


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    print(f"http://localhost:{port}")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
