# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['YeManTdpCtl.py'],
    pathex=[],
    binaries=[],
    datas=[('_internal/RyzenSMU.bin', '.'), ('_internal/AMDFamily17.bin', '.'), ('_internal/IntelMSR.bin', '.'), ('_internal/LpcIO.bin', '.'), ('_internal/COPYING', '.'), ('PawnIO_setup.exe', '.')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='YeManTdpCtl',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='YeManTdpCtl',
)
