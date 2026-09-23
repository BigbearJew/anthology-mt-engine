"""Check the production DX11 cache selection; never change a user's cache.

The source predicates matter here: bytecode CRC checks do not fingerprint HLSL
includes. Test both the generated-cache namespace and shipped-object bypass,
with negative controls for each regression.
"""

from itertools import product
from pathlib import Path, PureWindowsPath
import re


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/Layers/xrRenderPC_R4/r4.cpp"
PREFIX = "shaders_cache\\r4\\anthology_seasons146\\"


def selection(source):
    start = source.index("string_path temp_file_name, file_name;")
    end = source.index("if (FS.exist(file_name))", start)
    block = source[start:end]
    # There must be no route into an option-dependent or shipped variant cache.
    assert re.search(r"bool useGeneratedShaderCache\s*=\s*true\s*;", block)
    assert "if (!useGeneratedShaderCache)" in block
    assert block.index("if (!useGeneratedShaderCache)") < block.index("match_shader_id(")
    assert block.index("match_shader_id(") < block.index("if (useGeneratedShaderCache)")
    generated = block[block.index("if (useGeneratedShaderCache)"):block.index("\n\telse")]
    assert re.findall(r'xr_strcpy\(file, "([^"]+)"\);', generated) == [PREFIX.replace("\\", "\\\\")]
    assert 'FS.update_path(file_name, "$app_data_root$", file);' in generated
    assert re.findall(r'xr_strcat\(file, (.*)\);', generated) == [
        "name", '"."', "extension", '"\\\\"', "sh_name"]
    # Only the unreachable old branch may assign a shipped-object path.
    old_branch = block[block.index("\n\telse"):]
    assert old_branch.strip() == (
        'else\n\t{\n\t\txr_strcpy(file_name, folder_name);\n'
        '\t\txr_strcat(file_name, temp_file_name);\n\t}'
    )
    return PREFIX


source = SOURCE.read_text(encoding="utf-8")
prefix = selection(source)
checks = 0
for requested_precompiled, shipped_exists, old_exists, new_exists in product((False, True), repeat=4):
    for stage in ("vs", "ps", "gs", "hs", "ds", "cs"):
        # Production always takes the generated branch, independently of the
        # console flag and which previous bytecode files are present.
        variant = "204800100000001"
        shader = "deffer_base_bump"
        selected = PureWindowsPath(prefix, shader + "." + stage, variant)
        old = PureWindowsPath("shaders_cache/r4", shader + "." + stage, variant)
        shipped = PureWindowsPath("r3/objects/r4", shader + "." + stage, variant)
        files = {}
        if old_exists:
            files[old] = b"old-generated-bytecode"
        if shipped_exists:
            files[shipped] = b"old-shipped-bytecode"
        if new_exists:
            files[selected] = b"v144-bytecode"
        before = dict(files)
        assert selected != old and selected != shipped
        assert files.get(selected) == (b"v144-bytecode" if new_exists else None)
        assert files == before
        checks += 1

for bad in (
    source.replace("bool useGeneratedShaderCache = true;",
                   "bool useGeneratedShaderCache = psDeviceFlags2.test(rsPrecompiledShaders);"),
    source.replace(PREFIX.replace("\\", "\\\\"), "shaders_cache\\\\r4\\\\"),
):
    try:
        selection(bad)
    except AssertionError:
        checks += 1
    else:
        raise AssertionError("cache regression negative control was not rejected")

print(f"v146 shader cache: {checks} checks passed (source contract + path matrix + negative controls)")
