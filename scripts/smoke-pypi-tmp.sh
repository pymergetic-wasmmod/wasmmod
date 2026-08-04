#!/usr/bin/env bash
# Clean-room smoke: fresh wasmmod tree in /tmp + PyPI metal-cdn client (+ server).
#
# Proves publish tooling works without any local editable metal-cdn install.
#
#   ./scripts/smoke-pypi-tmp.sh
#   EXPECT_VER=0.1.0a4 ./scripts/smoke-pypi-tmp.sh
#   KEEP=1 ./scripts/smoke-pypi-tmp.sh
#
# Env:
#   EXPECT_VER     PyPI metal-cdn / client version (default: latest from PyPI JSON)
#   METAL_CDN_PORT default 18081
#   SOURCE         git|workdir — git = clone github main; workdir = copy this checkout
#   KEEP           1 = leave workdir + server
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${METAL_CDN_PORT:-18081}"
CDN_URL="http://127.0.0.1:${PORT}/cdn"
KEEP="${KEEP:-0}"
SOURCE="${SOURCE:-git}"
REPO_URL="${REPO_URL:-https://github.com/pymergetic/wasmmod.git}"
REPO_REF="${REPO_REF:-main}"

latest_pypi() {
  python3 - <<'PY'
import json, urllib.request
url = "https://pypi.org/pypi/pymergetic-metal-cdn-client/json"
with urllib.request.urlopen(url, timeout=30) as r:
    print(json.load(r)["info"]["version"])
PY
}

EXPECT_VER="${EXPECT_VER:-$(latest_pypi)}"
WORK="$(mktemp -d /tmp/wasmmod-pypi-smoke.XXXXXX)"
cleanup() {
  if [[ -n "${SERVER_PID:-}" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  if [[ "$KEEP" != "1" ]]; then
    rm -rf "$WORK"
  else
    echo "KEEP=1 → left $WORK (server pid ${SERVER_PID:-none})"
  fi
}
trap cleanup EXIT

echo "==> workdir $WORK"
echo "==> expect PyPI client/server $EXPECT_VER"
echo "==> source $SOURCE"

python3 -m venv "$WORK/venv"
# shellcheck disable=SC1091
source "$WORK/venv/bin/activate"
# Host ~/.config/pip/pip.conf adds pulp.prod… as extra-index; that mirror only
# has older alphas, and pip then reports “from versions: 0.1.0a3” only.
export PIP_CONFIG_FILE="/dev/null"
export PIP_INDEX_URL="https://pypi.org/simple"
export PIP_EXTRA_INDEX_URL=""
unset PIP_FIND_LINKS || true
python -m pip install -U pip -q

if [[ "$SOURCE" == "git" ]]; then
  echo "==> clone $REPO_URL @$REPO_REF (no local metal-cdn)"
  git clone --depth 1 --branch "$REPO_REF" "$REPO_URL" "$WORK/wasmmod"
  WASMMOD_DIR="$WORK/wasmmod"
elif [[ "$SOURCE" == "workdir" ]]; then
  echo "==> copy worktree $ROOT → $WORK/wasmmod"
  # Avoid .git / build junk; keep tools + examples + requirements.
  mkdir -p "$WORK/wasmmod"
  rsync -a \
    --exclude '.git' \
    --exclude '__pycache__' \
    --exclude '*.pyc' \
    --exclude '.venv' \
    --exclude 'ports/micropython/webassembly/build*' \
    "$ROOT/" "$WORK/wasmmod/"
  WASMMOD_DIR="$WORK/wasmmod"
else
  echo "FAIL: SOURCE must be git|workdir" >&2
  exit 1
fi

echo "==> pip install metal-cdn from PyPI (==$EXPECT_VER)"
python -m pip install --index-url https://pypi.org/simple \
  "pymergetic-metal-cdn-client==${EXPECT_VER}" \
  "pymergetic-metal-cdn==${EXPECT_VER}" -q

python - <<PY
from importlib.metadata import version
cv = version("pymergetic-metal-cdn-client")
sv = version("pymergetic-metal-cdn")
assert cv == "${EXPECT_VER}", cv
assert sv == "${EXPECT_VER}", sv
print("PyPI client", cv, "server", sv)
PY

echo "==> wasmmod requirements-publish.txt (must resolve to >= floor, got $EXPECT_VER)"
python -m pip install --index-url https://pypi.org/simple \
  -r "$WASMMOD_DIR/requirements-publish.txt" -q
python - <<PY
import sys
sys.path.insert(0, "$WASMMOD_DIR/tools")
from importlib.metadata import version
from wasmmod_cliutil import CLIENT_MIN_VERSION, client_version_ok, require_cdn_client
inst = version("pymergetic-metal-cdn-client")
print("CLIENT_MIN_VERSION", CLIENT_MIN_VERSION)
print("installed", inst)
assert client_version_ok(inst), (inst, CLIENT_MIN_VERSION)
assert inst == "${EXPECT_VER}", (inst, "${EXPECT_VER}")
mod = require_cdn_client("smoke")
print("require_cdn_client ok", getattr(mod, "__version__", inst))
PY

echo "==> start metal-cdn from PyPI on :$PORT"
export METAL_CDN_DATA_DIR="$WORK/data"
export METAL_CDN_STORAGE_ROOT="$WORK/data/packs"
export METAL_CDN_DATABASE_URL="sqlite+aiosqlite:///$WORK/data/metal_cdn.db"
export METAL_CDN_BASE_PATH="/cdn"
export METAL_CDN_HOST="127.0.0.1"
export METAL_CDN_PORT="$PORT"
export METAL_CDN_EXPERIMENTAL=true
export METAL_CDN_REQUIRE_AUTH=false
export METAL_CDN_SESSION_SECRET="wasmmod-smoke-secret"
mkdir -p "$METAL_CDN_STORAGE_ROOT"

metal-cdn serve >"$WORK/server.log" 2>&1 &
SERVER_PID=$!

for i in $(seq 1 40); do
  if curl -sf "$CDN_URL/health" >/dev/null; then
    break
  fi
  sleep 0.25
  if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "FAIL: server died" >&2
    tail -50 "$WORK/server.log" >&2
    exit 1
  fi
  if [[ "$i" -eq 40 ]]; then
    echo "FAIL: health timeout" >&2
    tail -50 "$WORK/server.log" >&2
    exit 1
  fi
done

curl -sf "$CDN_URL/health" | tee "$WORK/health.json"
python - <<PY
import json
from pathlib import Path
h = json.loads(Path("$WORK/health.json").read_text())
assert h.get("version") == "${EXPECT_VER}", h
print("health version", h.get("version"))
PY

echo "==> isolated HOME: register + claim via metal-cdn CLI"
export HOME="$WORK/home"
mkdir -p "$HOME"
metal-cdn login --url "$CDN_URL" --email "wasmmod-smoke@example.com" --password "smoke-smoke-1" --register
metal-cdn claim hellosmoke || true

echo "==> wasmmod publish --dry-run (from-artifacts if hello.wasm present)"
HELLO="$WASMMOD_DIR/examples/hello"
if [[ -f "$HELLO/hello.wasm" ]]; then
  # Login above wrote ~/.config/metal-cdn/config.json (url + token).
  export METAL_CDN_URL="$CDN_URL"
  python "$WASMMOD_DIR/tools/wasmmod.py" publish \
    --from-artifacts "$HELLO/hello.wasm" \
    --version 0.1.0-smoke \
    --no-sign \
    --dry-run | tee "$WORK/publish-dry.txt"
else
  echo "SKIP pack publish dry-run (no $HELLO/hello.wasm — build offline if needed)"
fi

echo "==> CdnClient health via wasmmod tools path"
python - <<PY
from pymergetic.metal.cdn_client import CdnClient
c = CdnClient("$CDN_URL")
print("CdnClient.health", c.health())
print("CdnClient.status experimental", c.status().get("experimental"))
PY

echo "PASS: clean wasmmod + PyPI $EXPECT_VER (source=$SOURCE)"
