"""Reads NIST ACVP-Server and Wycheproof test files for the Std.Crypto.Pqc generators.

The files are the `internalProjection.json` of each test set (inputs and
expected results together) at one pinned commit of
https://github.com/usnistgov/ACVP-Server, under gen-val/json-files/<name>/.
Wycheproof files are testvectors_v1/<name>.json of
https://github.com/C2SP/wycheproof at the pinned WYCHEPROOF_COMMIT. Each
download is checked against the SHA-256 recorded here, so a regenerated
vector file comes from exactly these bytes. Pass --json-dir to read (or keep)
the downloads in a directory instead of a temporary one.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import urllib.request

COMMIT = "975de31eb83d87039ec88934fdc47d8c312b892d"
BASE = f"https://raw.githubusercontent.com/usnistgov/ACVP-Server/{COMMIT}/gen-val/json-files/"
SHA256 = {
    "ML-KEM-keyGen-FIPS203": "d7a62a2c3476957f56dd8d24f9004ea6776ccfe995ffe71a65bb9506dc9c7b1b",
    "ML-KEM-encapDecap-FIPS203": "a556952ce869bb89c3a3196a701dad89647c193a34c86eafb61a9d710d5b810f",
    "ML-DSA-keyGen-FIPS204": "e67ee6540d40e11506c3c4e3b1f79fc1cefcd49820db99fc61f87cc8ba463baf",
    "ML-DSA-sigGen-FIPS204": "72dcaf5f69853ca267ccd16af9cb40949786aca0fcfbf05d1ebeba132b93af22",
    "ML-DSA-sigVer-FIPS204": "47cdd6314c7f746d02421ffcba89d4dbc7bb875ac49e07a029fdfc26fba55437",
    "SLH-DSA-keyGen-FIPS205": "d7c53a1b6450087047b57aae83a5a51a0ac89ecdb23ebe071e83fbb69ae9d920",
    "SLH-DSA-sigGen-FIPS205": "62b42e7c27fda5de8a94aba2943ae413b59b6ea08141f3bc035b1eceae235dba",
    "SLH-DSA-sigVer-FIPS205": "a013fc2104f4ed4799d96d51141f65b965969b2cf10646626a021b6d456ce792",
}


WYCHEPROOF_COMMIT = "65ca494727a52944c204441c860b039e6786b99b"
WYCHEPROOF_BASE = f"https://raw.githubusercontent.com/C2SP/wycheproof/{WYCHEPROOF_COMMIT}/testvectors_v1/"
WYCHEPROOF_SHA256 = {
    "mldsa_44_sign_seed_test": "cc12df1c6c7dd5591195151ed2ae98708bf5de2df24bb06dc00b031f7c60b8cd",
    "mldsa_44_verify_test": "f70759c95146ff3057ae9708438f6e8d25ba822e3af11d2e87bb20d9a9c39d0e",
    "mldsa_65_sign_seed_test": "e26b2bb9ab8d45ed570ba6828c7e40b6fa3e1545bb8bc2c449c77531760133e0",
    "mldsa_65_verify_test": "1ca235f61928421a173a171780beb00264ae9155dc33c52a11e1766cc7979034",
    "mldsa_87_sign_seed_test": "6e801c2916fee64e71cd845928513726cdbfe688ef41be4544e472e4d8742c8e",
    "mldsa_87_verify_test": "54adffe10f6dad7941ffd97f77ad4e5c85cb3a54d44cf5ce1c95cb1757b9ba14",
}


def fetch(path: pathlib.Path, url: str, expected: str) -> dict:
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
        with urllib.request.urlopen(request, timeout=120) as response:
            path.write_bytes(response.read())
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != expected:
        raise SystemExit(f"{path}: SHA-256 {digest} differs from the pinned {expected}")
    return json.loads(data)


def load_wycheproof(name: str, json_dir: pathlib.Path) -> dict:
    """The parsed Wycheproof file `name`, downloaded into json_dir/wycheproof/ if not there."""
    return fetch(json_dir / "wycheproof" / f"{name}.json", WYCHEPROOF_BASE + name + ".json", WYCHEPROOF_SHA256[name])


def load(name: str, json_dir: pathlib.Path) -> dict:
    """The parsed internalProjection.json of the test set `name`, downloaded if not in `json_dir`."""
    return fetch(json_dir / name / "internalProjection.json", BASE + name + "/internalProjection.json", SHA256[name])


def header(names: list[str]) -> list[str]:
    return [f"NIST ACVP-Server commit {COMMIT}, gen-val/json-files/<set>/internalProjection.json for:",
            "  " + ", ".join(names) + "."]
