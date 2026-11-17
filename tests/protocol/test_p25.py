#!/usr/bin/env python3
"""Compile portable CAI, compare pinned OP25, exercise FEC and stream loss.

Reference functions are loaded from the pristine source AST without executing
its CLI or import-time outfile creation. No RF hardware is touched.
"""
import ast
import contextlib
import ctypes as C
import hashlib
import io
import json
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
UP = ROOT / "research/protocol/op25"
PIN = "3f44d43eb449bb4753247ce848bb42a403823dea"
SEED = 0x2532
rng = random.Random(SEED)


class Call(C.Structure):
    _fields_ = [("nac", C.c_uint16), ("talkgroup", C.c_uint16),
                ("source_id", C.c_uint32), ("manufacturer_id", C.c_uint8),
                ("service_options", C.c_uint8), ("status_symbol", C.c_uint8)]


class NID(C.Structure):
    _fields_ = [("nac", C.c_uint16), ("duid", C.c_uint8), ("corrected_bits", C.c_uint8)]


class Decoded(C.Structure):
    _fields_ = [("nid", NID), ("metadata_valid", C.c_uint8), ("clear_voice", C.c_uint8),
                ("manufacturer_id", C.c_uint8), ("service_options", C.c_uint8),
                ("link_control_format", C.c_uint8), ("algorithm_id", C.c_uint8),
                ("key_id", C.c_uint16), ("talkgroup", C.c_uint16), ("source_id", C.c_uint32),
                ("message_indicator", C.c_uint8 * 9), ("low_speed_data", C.c_uint8 * 2),
                ("low_speed_valid", C.c_uint8 * 2), ("metadata_corrected_bits", C.c_uint16),
                ("metadata_corrected_symbols", C.c_uint8), ("voice_corrected_bits", C.c_uint8 * 9),
                ("voice_u0_corrected_bits", C.c_uint8 * 9), ("imbe", (C.c_uint16 * 8) * 9)]


class Sync(C.Structure):
    _fields_ = [("algorithm_id", C.c_uint8), ("key_id", C.c_uint16), ("message_indicator", C.c_uint8 * 9)]


class RX(C.Structure):
    _fields_ = [("sync_shift", C.c_uint64), ("symbols_seen", C.c_uint32),
                ("sync_acquisitions", C.c_uint32), ("complete_frames", C.c_uint32),
                ("bad_nids", C.c_uint32), ("truncated_frames", C.c_uint32),
                ("expected_nac", C.c_uint16), ("symbols", C.c_uint16),
                ("target_symbols", C.c_uint16), ("max_sync_errors", C.c_uint8),
                ("inverted", C.c_uint8), ("frame", C.c_uint8 * 216), ("nid", NID)]


def packed(bits):
    return bytes(sum(bits[i + j] << (7-j) for j in range(8)) for i in range(0, len(bits), 8))


def bits(data):
    return [(b >> shift) & 1 for b in data for shift in range(7, -1, -1)]


def physical(logical):
    return logical + 2 * (logical // 70)


def configure(lib):
    lib.p25_nid_encode.argtypes = [C.c_uint16, C.c_uint8]
    lib.p25_nid_encode.restype = C.c_uint64
    lib.p25_nid_decode.argtypes = [C.c_uint64, C.POINTER(NID)]
    lib.p25_decode_frame.argtypes = [C.c_void_p, C.c_size_t, C.POINTER(Decoded)]
    lib.p25_rs_encode.argtypes = [C.c_void_p, C.c_size_t, C.c_uint, C.c_void_p]
    lib.p25_rs_decode.argtypes = [C.c_void_p, C.c_size_t, C.c_uint]
    lib.p25_imbe_encode.argtypes = [C.c_void_p, C.c_void_p]
    lib.p25_imbe_decode.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
    lib.p25_rx_init.argtypes = [C.POINTER(RX), C.c_uint16, C.c_uint8]
    lib.p25_rx_push_dibit.argtypes = [C.POINTER(RX), C.c_uint8]
    lib.p25_encode_hdu_aes.argtypes = [C.c_void_p, C.c_void_p, C.c_void_p]
    lib.p25_encode_ldu_aes.argtypes = [C.c_void_p, C.c_void_p, C.c_uint8, C.c_void_p, C.c_void_p, C.c_void_p]


def reference():
    pin = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=UP, text=True).strip()
    assert pin == PIN, f"reference commit mismatch: {pin}"
    assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=UP, text=True).strip(), "reference checkout changed"
    path = UP / "op25/gr-op25_repeater/apps/tx/p25craft.py"
    tree = ast.parse(path.read_text())
    namespace = {"sys": __import__("sys"), "struct": __import__("struct"), "outfile": None,
                 "quiet": True, "flip": 0}
    exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n, ast.FunctionDef)], type_ignores=[]), str(path), "exec"), namespace)
    namespace["text_out"] = lambda _: None
    return namespace


def make_reference(ref, function, *args):
    sink = io.BytesIO()
    ref["outfile"] = sink
    with contextlib.redirect_stdout(io.StringIO()):
        ref[function](*args)
    return sink.getvalue()


def encode(lib, function, call, n, *args):
    out = (C.c_uint8 * n)()
    assert getattr(lib, function)(C.byref(call), *args, out) == 0
    return bytes(out)


def assert_metadata(lib, frame, duid, nac, tgid, source=None):
    out = Decoded()
    assert lib.p25_decode_frame(frame, len(frame), C.byref(out)) == 0
    assert out.nid.duid == duid and out.nid.nac == nac
    assert out.metadata_valid and out.clear_voice
    assert out.talkgroup == tgid if duid != 10 else out.algorithm_id == 0x80
    if source is not None: assert out.source_id == source
    return out


def channel_oracle(tmp):
    source = r'''
#include "op25_imbe_frame.h"
#include <cstdio>
extern "C" int oracle_encode(const unsigned short *u, unsigned char *out) {
    voice_codeword cw(144);
    imbe_header_encode(cw,u[0],u[1],u[2],u[3],u[4],u[5],u[6],unsigned(u[7])<<1);
    for (int i=0;i<18;++i) { out[i]=0; for(int j=0;j<8;++j)out[i]=(out[i]<<1)|cw[i*8+j]; }
    return 0;
}
extern "C" int oracle_decode(const unsigned char *in,unsigned short *u) {
    voice_codeword cw(144); unsigned a[8],e0,et;
    for(int i=0;i<144;++i)cw[i]=(in[i/8]>>(7-i%8))&1;
    imbe_header_decode(cw,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],e0,et);
    for(int i=0;i<8;++i)u[i]=a[i];u[7]>>=1;return et;
}
#include "ezpwd/rs"
extern "C" int oracle_rs(unsigned char *data,int n,int parity) {
    std::vector<unsigned char> v(data,data+n);
    int rc=-999;
    if(parity==8){ezpwd::RS<63,55>rs;rc=rs.decode(v);}
    if(parity==12){ezpwd::RS<63,51>rs;rc=rs.decode(v);}
    if(parity==16){ezpwd::RS<63,47>rs;rc=rs.decode(v);}
    for(int i=0;i<n;++i)data[i]=v[i];return rc;
}
'''
    path = tmp / "oracle.cc"
    path.write_text(source)
    out = tmp / "oracle.so"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-shared", "-fPIC",
                    "-I", str(UP / "op25/gr-op25_repeater/lib"), str(path), "-o", str(out)], check=True)
    lib = C.CDLL(str(out))
    lib.oracle_rs.argtypes = [C.c_void_p, C.c_int, C.c_int]
    return lib


def test_nid(lib, ref):
    total = 0
    for nac in range(4096):
        for duid in (0, 3, 5, 10, 15):
            code = ref["bch_64_16_23_encode"]((nac << 4) | duid)
            assert code == lib.p25_nid_encode(nac, duid)
            decoded = NID()
            assert lib.p25_nid_decode(code, C.byref(decoded)) == 0
            assert decoded.nac == nac and decoded.duid == duid and decoded.corrected_bits == 0
            total += 1
    for _ in range(1000):
        nac = rng.randrange(4096)
        duid = rng.choice((0, 3, 5, 10, 15))
        code = lib.p25_nid_encode(nac, duid)
        nerrors = rng.randrange(12)
        damaged = code ^ sum(1 << p for p in rng.sample(range(64), nerrors))
        decoded = NID()
        assert lib.p25_nid_decode(damaged, C.byref(decoded)) == 0
        assert (decoded.nac, decoded.duid, decoded.corrected_bits) == (nac, duid, nerrors)
    for duid in (1, 2, 4, 6, 7, 8, 9, 11, 12, 13, 14):
        assert lib.p25_nid_decode(lib.p25_nid_encode(0x293, duid), C.byref(NID())) < 0
    return {"all_nac_voice_duid_vectors": total, "random_0_to_11_error_vectors": 1000}


def test_rs(lib, ref, oracle):
    total = 0
    for count, parity, fn in ((20, 16, "rs_36_20_17_encode"), (12, 12, "rs_24_12_13_encode"), (16, 8, "rs_24_16_9_encode")):
        for _ in range(200):
            data = [rng.randrange(64) for _ in range(count)]
            number = 0
            for a in data: number = (number << 6) | a
            expected = ref[fn](number)
            out = (C.c_uint8 * (count + parity))()
            assert lib.p25_rs_encode(bytes(data), count, parity, out) == 0
            assert list(out) == expected
            for errors in range(parity // 2 + 1):
                damaged = bytearray(expected)
                for p in rng.sample(range(len(damaged)), errors): damaged[p] ^= rng.randrange(1, 64)
                a = (C.c_uint8 * len(damaged)).from_buffer_copy(damaged)
                b = (C.c_uint8 * len(damaged)).from_buffer_copy(damaged)
                assert lib.p25_rs_decode(a, len(a), parity) == errors
                assert oracle.oracle_rs(b, len(b), parity) == errors
                assert list(a) == list(b) == expected
                total += 1
        # Search deterministic beyond-capacity rejects; some >t errors can map
        # inside another codeword's radius and are not universally detectable.
        rejected = 0
        for _ in range(50):
            damaged = bytearray(expected)
            for p in rng.sample(range(len(damaged)), parity // 2 + 2): damaged[p] ^= rng.randrange(1, 64)
            arr = (C.c_uint8 * len(damaged)).from_buffer_copy(damaged)
            before = bytes(arr)
            rc = lib.p25_rs_decode(arr, len(arr), parity)
            if rc < 0:
                assert bytes(arr) == before
                rejected += 1
        assert rejected >= 45
    return {"p25craft_encode_and_ezpwd_decode_vectors": total}


def test_imbe(lib, oracle):
    for _ in range(1000):
        u = (C.c_uint16 * 8)(*[rng.randrange(1 << (12 if i < 4 else 11 if i < 7 else 7)) for i in range(8)])
        a, b = (C.c_uint8 * 18)(), (C.c_uint8 * 18)()
        assert lib.p25_imbe_encode(u, a) == 0
        oracle.oracle_encode(u, b)
        assert bytes(a) == bytes(b)
        # Inject up to t errors independently into all four Golay words and
        # three Hamming words. Leave the uncoded final 7 bits undamaged.
        raw = bits(a)
        expected_errors = 0
        for start, n, maximum in ((0, 23, 3), (23, 23, 3), (46, 23, 3), (69, 23, 3),
                                   (92, 15, 1), (107, 15, 1), (122, 15, 1)):
            count = rng.randrange(maximum + 1)
            expected_errors += count
            for p in rng.sample(range(start, start + n), count): raw[p] ^= 1
        damaged = packed(raw)
        v, w = (C.c_uint16 * 8)(), (C.c_uint16 * 8)()
        e0 = C.c_uint8()
        assert lib.p25_imbe_decode(damaged, v, C.byref(e0)) == expected_errors
        # OP25's Hamming helper counts corrected data bits but omits errors
        # confined to parity bits. Our return counts every corrected air bit.
        assert 0 <= oracle.oracle_decode(damaged, w) <= expected_errors
        assert list(v) == list(w) == list(u)
    return {"op25_channel_encode_decode_random_vectors": 1000}


def test_interleave():
    text = (UP / "op25/gr-op25_repeater/lib/op25_imbe_frame.h").read_text()
    section = text.split("voice_codeword_bits[nof_voice_codewords][voice_codeword_sz] = {")[1].split("};", 1)[0]
    original = [int(n) for n in re.findall(r"\d+", section)]
    assert len(original) == 9 * 144
    for frame in range(9):
        start = 112 + 144 * frame if frame < 2 else 440 + 184 * (frame - 2) if frame < 8 else 1536
        for j in range(144):
            p = physical(start + 6 * (j % 24) + ((j // 24) ^ (j & 1)))
            assert original[frame * 144 + j] == p
    return {"op25_interleave_positions": len(original)}


def test_frames(lib, ref):
    fixtures = []
    outdir = ROOT / "tests/protocol/vectors"
    outdir.mkdir(exist_ok=True)
    for idx in range(50):
        nac, tg, src = (0x293, 123, 123456) if idx == 0 else (rng.randrange(4096), rng.randrange(65536), rng.randrange(1 << 24))
        ss = 0 if idx == 0 else rng.randrange(4)
        call = Call(nac, tg, src, 0, 0, ss)
        hdu = encode(lib, "p25_encode_hdu", call, 99)
        assert hdu == make_reference(ref, "construct_hdu", nac, ss, 0, 0, 0x80, 0, tg)
        assert_metadata(lib, hdu, 0, nac, tg)
        tdu = encode(lib, "p25_encode_tdu", call, 18)
        assert tdu == make_reference(ref, "construct_stdu", nac, ss)
        tdulc = encode(lib, "p25_encode_tdulc", call, 54)
        assert tdulc == make_reference(ref, "construct_xtdu", nac, ss, 0, 0, 0, 0, tg, 0, src)
        assert_metadata(lib, tdulc, 15, nac, tg, src)
        # P25craft repeats an already channel-interleaved IMBE144 word. Our
        # encoder accepts nine parameter vectors. Give every vector identical
        # data except the protocol-owned sync bit; compare full over-air bytes.
        u = [0x321, 0x456, 0x789, 0xabc, 0x123, 0x456, 0x789, 0x5a] if idx == 0 else [rng.randrange(1 << (12 if i < 4 else 11 if i < 7 else 7)) for i in range(8)]
        u[7] &= ~1
        frames = ((C.c_uint16 * 8) * 9)(*[(C.c_uint16 * 8)(*u) for _ in range(9)])
        cw = (C.c_uint8 * 18)()
        lib.p25_imbe_encode(frames[0], cw)
        # Convert sequential coded bits to the local 144-bit interleave.
        inner = [0] * 144
        for j, bit in enumerate(bits(cw)): inner[6 * (j % 24) + ((j // 24) ^ (j & 1))] = bit
        imbe = int.from_bytes(packed(inner), "big")
        lsd_value = 0x12345678 if idx == 0 else rng.randrange(1 << 32)
        ldus = []
        for duid in (5, 10):
            pair = (C.c_uint8 * 2)(*lsd_value.to_bytes(4, "big")[(0 if duid == 5 else 2):(2 if duid == 5 else 4)])
            ldu = encode(lib, "p25_encode_ldu", call, 216, duid, frames, pair)
            if duid == 5:
                expected = make_reference(ref, "construct_ldu1", nac, ss, imbe, lsd_value, 0, 0, 0, 0, tg, 0, src)
            else:
                expected = make_reference(ref, "construct_ldu2", nac, ss, imbe, lsd_value, 0, 0x80, 0)
            assert ldu == expected
            decoded = assert_metadata(lib, ldu, duid, nac, tg, src if duid == 5 else None)
            for i in range(9):
                assert list(decoded.imbe[i])[:7] == u[:7]
                assert decoded.imbe[i][7] == (u[7] | ((i + (duid == 10)) & 1))
            assert list(decoded.low_speed_data) == list(pair) and all(decoded.low_speed_valid)
            ldus.append(ldu)
        if idx == 0:
            for name, data in (("hdu", hdu), ("ldu1", ldus[0]), ("ldu2", ldus[1]), ("tdu", tdu), ("tdulc", tdulc)):
                path = outdir / f"nac293-{name}.bin"
                path.write_bytes(data)
                fixtures.append({"path": str(path.relative_to(ROOT)), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
            (outdir / "clear-call.bin").write_bytes(hdu + b"".join(ldus) + tdu)
    return {"full_frame_p25craft_vectors": 250, "fixtures": fixtures}


def test_encrypted_frames(lib, ref):
    """HDU, LDU1 and LDU2 of an AES-256 call (ALGID 0x84, random MI and KID) against OP25's construct_hdu,
    construct_ldu1 and construct_ldu2: the encryption sync signaling, which test_frames cannot exercise (its MI, ALGID and
    KID are all zero, so a wrong byte order of the MI or a wrong KID position would pass). What a receiver reads the MI and
    KID from when the radio transmits."""
    n = 60
    for idx in range(n):
        nac, tg, src = rng.randrange(4096), rng.randrange(1, 65536), rng.randrange(1, 1 << 24)
        ss, kid = rng.randrange(4), rng.randrange(1, 65536)
        # a 64-bit MI in the first eight bytes of the 72-bit field; every other call also has a ninth byte, which
        # the IV does not use but a radio may send and the encoder carries as given
        mi72 = (rng.randrange(1, 1 << 64) << 8) | (rng.randrange(1, 256) if idx % 2 else 0)
        call = Call(nac, tg, src, 0, 0x40, ss)
        sync = Sync(0x84, kid, (C.c_uint8 * 9)(*mi72.to_bytes(9, "big")))
        hdu = (C.c_uint8 * 99)()
        assert lib.p25_encode_hdu_aes(C.byref(call), C.byref(sync), hdu) == 0
        assert bytes(hdu) == make_reference(ref, "construct_hdu", nac, ss, mi72, 0, 0x84, kid, tg), f"HDU {idx}"
        u = [rng.randrange(1 << (12 if i < 4 else 11 if i < 7 else 7)) for i in range(8)]
        u[7] &= ~1
        probe = ((C.c_uint16 * 8) * 9)()
        for i in range(9):
            for j in range(8): probe[i][j] = u[j]
        cw = (C.c_uint8 * 18)()
        lib.p25_imbe_encode(probe[0], cw)
        inner = [0] * 144
        for j, bit in enumerate(bits(cw)): inner[6 * (j % 24) + ((j // 24) ^ (j & 1))] = bit
        imbe = int.from_bytes(packed(inner), "big")
        lsd_value = rng.randrange(1 << 32)
        for duid in (5, 10):
            # Encrypted, the last bit of each frame is ciphertext and is the caller's; give it the value both sides write
            frames = ((C.c_uint16 * 8) * 9)()
            for i in range(9):
                for j in range(8): frames[i][j] = u[j]
                frames[i][7] = u[7] | ((i + (duid == 10)) & 1)
            pair = (C.c_uint8 * 2)(*lsd_value.to_bytes(4, "big")[(0 if duid == 5 else 2):(2 if duid == 5 else 4)])
            ldu = (C.c_uint8 * 216)()
            assert lib.p25_encode_ldu_aes(C.byref(call), C.byref(sync), duid, frames, pair, ldu) == 0
            if duid == 5:
                expected = make_reference(ref, "construct_ldu1", nac, ss, imbe, lsd_value, 0, 0, 0x40, 0, tg, 0, src)
            else:
                expected = make_reference(ref, "construct_ldu2", nac, ss, imbe, lsd_value, mi72, 0x84, kid)
            assert bytes(ldu) == expected, f"LDU{1 if duid == 5 else 2} {idx}"
            out = Decoded()
            assert lib.p25_decode_frame(bytes(ldu), 216, C.byref(out)) == 0 and out.metadata_valid
            if duid == 10:
                assert bytes(out.message_indicator) == mi72.to_bytes(9, "big") and out.key_id == kid
                assert out.algorithm_id == 0x84 and not out.clear_voice
    return {"op25_encrypted_hdu_ldu1_ldu2_calls": n}


def test_aes_keystream(tmp):
    """The firmware's P25 AES-256 keystream, frame by frame, against OP25's own op25_crypt_aes (the class that decrypts
    Motorola AES traffic): for random keys and MIs, the 11 bytes it XORs into each of the 18 IMBE frames of a superframe.
    The firmware's own vectors were computed by the firmware, so on their own they only show that it agrees with itself."""
    lib_dir = UP / "op25/gr-op25_repeater/lib"
    stubs = tmp / "stubs" / "gnuradio"
    stubs.mkdir(parents=True)
    (stubs / "msg_queue.h").write_text("// stub: OP25's crypto classes include it and do not use it for the keystream\n")
    (tmp / "op25_masks.cc").write_text(r'''
#include <cstring>
#include <vector>
#include "op25_crypt_aes.h"
extern "C" int op25_aes_masks(const unsigned char *key, const unsigned char *mi, unsigned short kid, unsigned char *out) {
    static log_ts lg;
    static op25_crypt_aes aes(lg, 0, 0);
    aes.key(kid, ALG_AES_256, std::vector<uint8_t>(key, key + 32));
    uint8_t MI[9];
    memcpy(MI, mi, 9);
    for (int ldu = 0; ldu < 2; ldu++) {
        if (!aes.prepare(kid, PT_P25_PHASE1, MI)) return -1;
        for (int i = 0; i < 9; i++) {
            packed_codeword pcw(11, 0);
            aes.process(pcw, ldu ? FT_LDU2 : FT_LDU1, i);
            memcpy(out + (ldu * 9 + i) * 11, pcw.data(), 11);
        }
    }
    return 0;
}
''')
    op25 = tmp / "op25_masks.so"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++14", "-O1", "-w", "-shared", "-fPIC", "-include", "cstdint",
                    "-include", "cstdio", "-I", str(tmp / "stubs"), "-I", str(lib_dir), str(tmp / "op25_masks.cc"),
                    *(str(lib_dir / f"op25_crypt_{n}.cc") for n in ("aes", "algs", "des", "adp")), "-o", str(op25)],
                   check=True)
    (tmp / "fw_masks.c").write_text(r'''
#include <string.h>
#include "voice_aes.h"
int fw_aes_masks(const unsigned char *key, const unsigned char *mi, unsigned char *out) {
    uint64_t m = 0;
    voice_aes s;
    for (int i = 0; i < 8; i++) m = (m << 8) | mi[i];
    voice_aes_p25_start(&s, key, m);
    for (unsigned f = 0; f < 18; f++) {
        uint16_t u[8] = {0};
        unsigned bit = 0;
        memset(out + f * 11, 0, 11);
        voice_aes_p25_frame(&s, f, u);
        for (unsigned w = 0; w < 8; w++) {
            unsigned width = w < 4 ? 12 : w < 7 ? 11 : 7;
            for (unsigned j = 0; j < width; j++, bit++)
                out[f * 11 + bit / 8] |= (unsigned char)(((u[w] >> (width - 1 - j)) & 1u) << (7 - bit % 8));
        }
    }
    return 0;
}
''')
    fw = tmp / "fw_masks.so"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-O2", "-w", "-shared", "-fPIC", "-I", str(ROOT / "src/crypto"),
                    str(tmp / "fw_masks.c"), str(ROOT / "src/crypto/aes.c"), str(ROOT / "src/crypto/voice_aes.c"),
                    "-o", str(fw)], check=True)
    a, b = C.CDLL(str(op25)), C.CDLL(str(fw))
    for f in (a.op25_aes_masks, b.fw_aes_masks):
        f.restype = C.c_int
    a.op25_aes_masks.argtypes = [C.c_char_p, C.c_char_p, C.c_uint16, C.c_void_p]
    b.fw_aes_masks.argtypes = [C.c_char_p, C.c_char_p, C.c_void_p]
    n = 300
    for idx in range(n):
        key = bytes(rng.randrange(256) for _ in range(32))
        # the ninth byte of the 72-bit MI is not part of the AES IV: some of the cases carry a nonzero one
        mi = bytes(rng.randrange(256) for _ in range(8)) + bytes([rng.randrange(256) if idx % 3 == 0 else 0])
        want, got = (C.c_uint8 * 198)(), (C.c_uint8 * 198)()
        assert a.op25_aes_masks(key, mi, 1 + idx, want) == 0 and b.fw_aes_masks(key, mi, got) == 0
        assert bytes(got) == bytes(want), f"keystream differs from OP25 for key {key.hex()} MI {mi.hex()}"
    return {"op25_aes256_keystream_random_cases": n}


def test_stream(lib):
    path = ROOT / "tests/protocol/vectors/clear-call.bin"
    data = path.read_bytes()
    dibits = [(b >> s) & 3 for b in data for s in (6, 4, 2, 0)]
    for inverted in (False, True):
        rx = RX()
        lib.p25_rx_init(C.byref(rx), 0x293, 0)
        seen = []
        for d in [0] * 31 + dibits:
            if lib.p25_rx_push_dibit(C.byref(rx), d ^ (2 if inverted else 0)) == 1:
                seen.append((rx.nid.nac, rx.nid.duid, bytes(rx.frame[:lib.p25_frame_bytes(rx.nid.duid)])))
        assert [n[1] for n in seen] == [0, 5, 10, 3]
        assert rx.bad_nids == 0 and rx.truncated_frames == 0
        assert rx.complete_frames == 4
    rx = RX()
    lib.p25_rx_init(C.byref(rx), 0x123, 0)
    for d in dibits: assert lib.p25_rx_push_dibit(C.byref(rx), d) == 0
    assert rx.bad_nids == 4
    # Drop body bits, retain the next frame sync: truncated frames must never
    # be emitted as complete. Reacquire next complete LDU2 and terminator.
    rx = RX()
    lib.p25_rx_init(C.byref(rx), 0x293, 0)
    damaged = dibits[:396 + 200] + dibits[396 + 864:]
    seen = []
    for d in damaged:
        if lib.p25_rx_push_dibit(C.byref(rx), d) == 1: seen.append(rx.nid.duid)
    assert seen == [0, 10, 3] and rx.truncated_frames == 1
    # Two errors in frame sync are tolerated but wrong NAC is still rejected.
    rx = RX()
    lib.p25_rx_init(C.byref(rx), 0x293, 2)
    damaged = dibits[:]
    damaged[3] ^= 1; damaged[8] ^= 2
    seen = []
    for d in damaged:
        if lib.p25_rx_push_dibit(C.byref(rx), d) == 1: seen.append(rx.nid.duid)
    assert seen == [0, 5, 10, 3]
    assert lib.p25_rx_push_dibit(C.byref(rx), 4) == -1
    return {"stream_cases": ["normal", "inverted", "wrong_nac", "truncated_reacquisition", "two_sync_errors", "invalid_dibit"]}


def test_damage(lib, ref):
    path = ROOT / "tests/protocol/vectors"
    hdu = (path / "nac293-hdu.bin").read_bytes()
    ldu1 = (path / "nac293-ldu1.bin").read_bytes()
    for count in (1, 4, 8):
        raw = bits(hdu)
        # Four errors within one shortened Golay word exceed its t=3. The
        # outer RS layer must still repair the erroneous systematic symbol.
        for symbol in range(count):
            for bit in range(4): raw[physical(112 + 18 * symbol + bit)] ^= 1
        decoded = assert_metadata(lib, packed(raw), 0, 0x293, 123)
        assert decoded.metadata_corrected_symbols == count
    raw = bits(hdu)
    for symbol in range(9):
        for bit in range(4): raw[physical(112 + 18 * symbol + bit)] ^= 1
    decoded = Decoded()
    assert lib.p25_decode_frame(packed(raw), len(hdu), C.byref(decoded)) == 0
    assert not decoded.metadata_valid and not decoded.clear_voice
    for count in (1, 3, 6):
        raw = bits(ldu1)
        for symbol in range(count):
            start = 400 + 184 * (symbol // 4) + 10 * (symbol % 4)
            for bit in range(2): raw[physical(start + bit)] ^= 1
        decoded = assert_metadata(lib, packed(raw), 5, 0x293, 123, 123456)
        assert decoded.metadata_corrected_symbols == count
    # Ciphered metadata is identified for audio gating; no encryption code.
    encrypted = make_reference(ref, "construct_hdu", 0x293, 1, 0, 0, 0x84, 1, 123)
    assert lib.p25_decode_frame(encrypted, len(encrypted), C.byref(decoded)) == 0
    assert decoded.metadata_valid and not decoded.clear_voice
    assert decoded.algorithm_id == 0x84 and decoded.key_id == 1
    for size in (0, 1, 17, 18, 98, 100, 215, 216):
        assert lib.p25_decode_frame(hdu + bytes(217), size, C.byref(decoded)) < 0
    invalid_sync = bytes(6) + hdu[6:]
    assert lib.p25_decode_frame(invalid_sync, len(hdu), C.byref(decoded)) < 0
    # Standard MFID1 has the same standard-format LC definition as MFID0.
    standard_one = Call(0x293, 123, 123456, 1, 0, 0)
    tdulc = encode(lib, "p25_encode_tdulc", standard_one, 54)
    assert tdulc == make_reference(ref, "construct_xtdu", 0x293, 0, 0, 1, 0, 0, 123, 0, 123456)
    decoded = assert_metadata(lib, tdulc, 15, 0x293, 123, 123456)
    assert decoded.manufacturer_id == 1
    return {"damaged_metadata_cases": 7, "encrypted_audio_flag": "clear_voice_false", "invalid_lengths_and_sync": "rejected"}


def main():
    subprocess.run([__import__("sys").executable, str(ROOT / "src/p25/generate_tables.py"), "--check"], check=True)
    ref = reference()
    results = {"reference_op25_commit": PIN, "random_seed": SEED, "hardware_verified": False}
    with tempfile.TemporaryDirectory(prefix="dm32-p25-test-") as dirname:
        tmp = Path(dirname)
        output = tmp / "libp25.so"
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2", "-shared", "-fPIC",
                        str(ROOT / "src/p25/p25.c"), "-o", str(output)], check=True)
        lib = C.CDLL(str(output))
        configure(lib)
        oracle = channel_oracle(tmp)
        for name, function, args in (("nid", test_nid, (lib, ref)), ("rs", test_rs, (lib, ref, oracle)),
                                    ("imbe", test_imbe, (lib, oracle)), ("interleave", test_interleave, ()),
                                    ("frames", test_frames, (lib, ref)),
                                    ("encrypted_frames", test_encrypted_frames, (lib, ref)),
                                    ("aes_keystream", test_aes_keystream, (tmp,)), ("stream", test_stream, (lib,)),
                                    ("damage", test_damage, (lib, ref))):
            results[name] = function(*args)
            print(f"PASS {name}")
    results["status"] = "passed"
    report = ROOT / "tests/protocol/results.json"
    report.write_text(json.dumps(results, indent=2) + "\n")
    print(report)


if __name__ == "__main__": main()
