#!/usr/bin/env python3

# This file is part of wasmmod, https://github.com/pymergetic/wasmmod
#
# The MIT License (MIT)
#
# Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
"""
Sign .wasm / .aot for MICROPY_WASM_VERIFY (ECDSA-P256 + SHA-256).

PKI (preferred) — trust is host-scoped; sign is per artifact:
  tools/wasmmod.py sign gen-pki -o .keys                 # → trust/ + sign/
  tools/wasmmod.py sign gen-pki -o .keys --sub-ca        # root → sub-CA → leaf
  tools/wasmmod.py sign sign --key .keys/sign/leaf.key.pem \\
      --chain .keys/sign/chain.der packs/hello.wasm
  # → hello.wasm.sig + hello.wasm.crt (leaf [+ intermediates])
  # Host: wasm.add_trust(open(".keys/trust/root.crt.der","rb").read())

Raw pubkey (still supported):
  tools/wasmmod.py sign gen-key -o .keys/dev
  tools/wasmmod.py sign sign --key .keys/dev.pem packs/hello.wasm

Requires openssl.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import textwrap
from pathlib import Path

SIG_SECTION = "wasmmod.sig"
# Embedded / future: magic + sig + optional cert chain (leaf first).
MPWS_MAGIC = b"MPWS"
MPWS_VER = 1


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), file=sys.stderr)
    subprocess.check_call(cmd)


def uleb(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def write_file(path: Path, data: bytes | str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        path.write_text(data)
    else:
        path.write_bytes(data)


def openssl_sign(key: Path, data: Path, sig_out: Path) -> None:
    run(["openssl", "dgst", "-sha256", "-sign", str(key), "-out", str(sig_out), str(data)])


def pem_to_der_cert(pem: Path, der: Path) -> None:
    run(["openssl", "x509", "-in", str(pem), "-outform", "DER", "-out", str(der)])


def cmd_gen_key(out_prefix: Path) -> None:
    out_prefix.parent.mkdir(parents=True, exist_ok=True)
    pem = out_prefix.with_suffix(".pem")
    pub = Path(str(out_prefix) + ".pub.der")
    run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(pem)])
    run(["openssl", "ec", "-in", str(pem), "-pubout", "-outform", "DER", "-out", str(pub)])
    print(pem)
    print(pub)


def _ec_key(path: Path) -> None:
    run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(path)])


def _req(key: Path, subject: str, out_csr: Path) -> None:
    run(["openssl", "req", "-new", "-key", str(key), "-subj", subject, "-out", str(out_csr)])


def _sign_cert(
    csr: Path,
    ca_crt: Path,
    ca_key: Path,
    out_crt: Path,
    *,
    days: int,
    extfile: Path,
    serial: int,
) -> None:
    run(
        [
            "openssl",
            "x509",
            "-req",
            "-in",
            str(csr),
            "-CA",
            str(ca_crt),
            "-CAkey",
            str(ca_key),
            "-set_serial",
            str(serial),
            "-days",
            str(days),
            "-sha256",
            "-extfile",
            str(extfile),
            "-out",
            str(out_crt),
        ]
    )


def cmd_gen_pki(out_dir: Path, with_sub_ca: bool, days: int) -> None:
    """Write trust/ (host CA) and sign/ (pack leaf + chain) under out_dir."""
    trust = out_dir / "trust"
    sign = out_dir / "sign"
    trust.mkdir(parents=True, exist_ok=True)
    sign.mkdir(parents=True, exist_ok=True)

    root_key = trust / "root.key.pem"
    root_crt = trust / "root.crt.pem"
    root_der = trust / "root.crt.der"
    leaf_key = sign / "leaf.key.pem"
    leaf_crt = sign / "leaf.crt.pem"
    leaf_der = sign / "leaf.crt.der"
    chain_der = sign / "chain.der"

    # Root CA (self-signed). OpenSSL 3 req uses -config/-extensions, not -extfile.
    _ec_key(root_key)
    root_cfg = trust / "root.cnf"
    write_file(
        root_cfg,
        textwrap.dedent(
            """\
            [req]
            distinguished_name = req_dn
            x509_extensions = v3_ca
            prompt = no
            [req_dn]
            CN = wasmmod-root
            [v3_ca]
            basicConstraints = critical,CA:TRUE,pathlen:2
            keyUsage = critical,keyCertSign,cRLSign
            subjectKeyIdentifier = hash
            """
        ),
    )
    run(
        [
            "openssl",
            "req",
            "-new",
            "-x509",
            "-key",
            str(root_key),
            "-config",
            str(root_cfg),
            "-days",
            str(days),
            "-sha256",
            "-out",
            str(root_crt),
        ]
    )
    pem_to_der_cert(root_crt, root_der)

    signer_crt = root_crt
    signer_key = root_key
    serial = 2
    intermediates: list[Path] = []

    if with_sub_ca:
        sub_key = sign / "sub.key.pem"
        sub_csr = sign / "sub.csr.pem"
        sub_crt = sign / "sub.crt.pem"
        sub_der = sign / "sub.crt.der"
        sub_ext = sign / "sub.ext"
        _ec_key(sub_key)
        _req(sub_key, "/CN=wasmmod-sub", sub_csr)
        write_file(
            sub_ext,
            textwrap.dedent(
                """\
                basicConstraints=critical,CA:TRUE,pathlen:0
                keyUsage=critical,keyCertSign,cRLSign
                subjectKeyIdentifier=hash
                authorityKeyIdentifier=keyid,issuer
                """
            ),
        )
        _sign_cert(sub_csr, root_crt, root_key, sub_crt, days=days, extfile=sub_ext, serial=serial)
        serial += 1
        pem_to_der_cert(sub_crt, sub_der)
        signer_crt, signer_key = sub_crt, sub_key
        intermediates.append(sub_der)

    # Leaf signing cert (digitalSignature only — not a CA).
    leaf_csr = sign / "leaf.csr.pem"
    leaf_ext = sign / "leaf.ext"
    _ec_key(leaf_key)
    _req(leaf_key, "/CN=wasmmod-pack-signer", leaf_csr)
    write_file(
        leaf_ext,
        textwrap.dedent(
            """\
            basicConstraints=critical,CA:FALSE
            keyUsage=critical,digitalSignature
            extendedKeyUsage=codeSigning
            subjectKeyIdentifier=hash
            authorityKeyIdentifier=keyid,issuer
            """
        ),
    )
    _sign_cert(leaf_csr, signer_crt, signer_key, leaf_crt, days=days, extfile=leaf_ext, serial=serial)
    pem_to_der_cert(leaf_crt, leaf_der)

    # chain.der = leaf || intermediates (root stays in trust/ only).
    chain = leaf_der.read_bytes() + b"".join(p.read_bytes() for p in intermediates)
    write_file(chain_der, chain)

    for p in (root_der, leaf_key, leaf_der, chain_der):
        print(p)
    if with_sub_ca:
        print(sign / "sub.crt.der")


def pack_mpws(sig: bytes, chain: bytes) -> bytes:
    if len(sig) > 0xFFFF or len(chain) > 0xFFFF:
        raise SystemExit("sig/chain too large for MPWS")
    return (
        MPWS_MAGIC
        + bytes([MPWS_VER, 0])
        + len(sig).to_bytes(2, "big")
        + sig
        + len(chain).to_bytes(2, "big")
        + chain
    )


def append_sig_section(wasm: bytes, payload: bytes) -> bytes:
    if len(wasm) < 8 or wasm[:4] != b"\x00asm":
        raise SystemExit("not a Wasm module (embed needs .wasm, not .aot)")
    name = SIG_SECTION.encode()
    body = uleb(len(name)) + name + payload
    section = bytes([0]) + uleb(len(body)) + body
    return wasm + section


def cmd_sign(
    key: Path,
    target: Path,
    *,
    embed: bool,
    cert: Path | None,
    chain: Path | None,
) -> None:
    if not target.is_file():
        raise SystemExit(f"missing {target}")
    sig_path = Path(str(target) + ".sig")
    openssl_sign(key, target, sig_path)
    print(sig_path)

    chain_bytes = b""
    if chain is not None:
        if not chain.is_file():
            raise SystemExit(f"missing chain {chain}")
        chain_bytes = chain.read_bytes()
    elif cert is not None:
        if not cert.is_file():
            raise SystemExit(f"missing cert {cert}")
        chain_bytes = cert.read_bytes()
    if chain_bytes:
        crt_path = Path(str(target) + ".crt")
        write_file(crt_path, chain_bytes)
        print(crt_path)

    if embed:
        data = target.read_bytes()
        sig = sig_path.read_bytes()
        payload = pack_mpws(sig, chain_bytes) if chain_bytes else sig
        target.write_bytes(append_sig_section(data, payload))
        print(f"{target} (+{SIG_SECTION})", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("gen-key", help="raw ECDSA keypair (no X.509)")
    g.add_argument("-o", "--output", type=Path, required=True, help="key prefix")

    p = sub.add_parser("gen-pki", help="write trust/ (root CA) + sign/ (leaf + chain)")
    p.add_argument("-o", "--output", type=Path, required=True, help="keys root (creates trust/ and sign/)")
    p.add_argument("--sub-ca", action="store_true", help="insert intermediate CA under root")
    p.add_argument("--days", type=int, default=3650, help="cert lifetime (default 10y)")

    s = sub.add_parser("sign", help="detach-sign → .sig [+.crt]; optional --embed")
    s.add_argument("--key", type=Path, required=True, help="private key PEM (leaf or raw)")
    s.add_argument("--cert", type=Path, help="leaf cert DER to ship as target.crt")
    s.add_argument(
        "--chain",
        type=Path,
        help="full chain DER (leaf first, then intermediates); overrides --cert content",
    )
    s.add_argument(
        "--embed",
        action="store_true",
        help="append wasmmod.sig section (.wasm only)",
    )
    s.add_argument("target", type=Path, help=".wasm / .aot path")

    args = ap.parse_args()
    if args.cmd == "gen-key":
        cmd_gen_key(args.output)
    elif args.cmd == "gen-pki":
        cmd_gen_pki(args.output, args.sub_ca, args.days)
    else:
        embed = bool(args.embed)
        if embed and args.target.suffix.lower() == ".aot":
            print("note: --embed ignored for .aot (use detached .sig/.crt)", file=sys.stderr)
            embed = False
        cmd_sign(args.key, args.target, embed=embed, cert=args.cert, chain=args.chain)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
