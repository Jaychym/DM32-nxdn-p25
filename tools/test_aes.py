#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""AES known-answer and voice protocol tests; no hardware access."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    out=ROOT/'build/aes-host';out.mkdir(parents=True,exist_ok=True)
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Isrc/crypto','-Isrc/p25']
    subprocess.run(['clang',*flags,'src/crypto/aes.c','src/crypto/voice_aes.c','src/p25/p25.c','tests/test_aes.c','-o',str(out/'test_aes')],cwd=ROOT,check=True)
    subprocess.run([str(out/'test_aes')],cwd=ROOT,check=True)
    codec=ROOT/'experiments/codec/out/ubsan/libdm32_imbe.a'
    if not codec.exists():
        subprocess.run(['python3','experiments/codec/build.py','--full-ubsan'],cwd=ROOT,check=True)
    subprocess.run(['clang',*flags,'-DCONFIG_DV_AES=1','-Isrc/codec','src/p25/p25.c','src/p25/voice.c',
                    'src/crypto/aes.c','src/crypto/voice_aes.c','tests/test_aes_voice.c',str(codec),'-lstdc++',
                    '-o',str(out/'test_aes_voice')],cwd=ROOT,check=True)
    subprocess.run([str(out/'test_aes_voice')],cwd=ROOT,check=True)
    rf=ROOT/'build/radiofoundry-p25'
    if not rf.exists(): raise SystemExit('Prepare the derivative tree first')
    sources=['core/codeplug/codeplug.c','core/storage/storage.c','core/settings/settings.c','tests/host/sim/sim_flash.c',
             'tests/host/sim/dmr_sim_modem.c','core/dmr/call/dmr_call.c','core/dmr/call/dmr_lc.c',
             'core/dmr/ambe/ambeFec.c','core/dmr/dmrData.c','core/dmr/fec/dmrFec.c','core/hotspot/dmrDataFrame.c']
    subprocess.run(['clang',*flags,'-Wno-unused-parameter','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
                    '-DCONFIG_DV_AES=1','-DCONFIG_DMR=1','-DCONFIG_SFR=0','-DDMR_RX_TRACE=0',
                    '-I'+str(rf/'core'),'-I'+str(rf/'hal'),'-I'+str(rf/'tests/host'),
                    'src/crypto/aes.c','src/crypto/voice_aes.c','src/crypto/dmr_aes.c',*[str(rf/p) for p in sources],
                    'tests/test_aes_radio.c','-o',str(out/'test_aes_radio')],cwd=ROOT,check=True)
    subprocess.run([str(out/'test_aes_radio')],cwd=ROOT,check=True)
if __name__=='__main__':main()
