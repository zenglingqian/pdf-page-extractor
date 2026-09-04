# 验证提取后的 PDF：解压每页内容流，检查页码文本
import zlib, re, sys

def check(path, expected):
    data = open(path, 'rb').read()
    streams = re.findall(rb'stream\r?\n(.*?)\r?\nendstream', data, re.S)
    found = []
    for s in streams:
        try:
            dec = zlib.decompress(s)
            m = re.search(rb'\(Page (\d+)\)', dec)
            if m:
                found.append(int(m.group(1)))
        except Exception:
            pass
    ok = found == expected
    print(f"{path}: pages={found} expected={expected} -> {'PASS' if ok else 'FAIL'}")
    return ok

ok = True
ok &= check('tests/out_3_5_9-10.pdf', [3, 5, 9, 10])
sys.exit(0 if ok else 1)
