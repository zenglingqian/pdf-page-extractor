# 生成真实世界常见结构的测试 PDF：
# 变体1: xref 流 + 对象流（ObjStm）—— 现代生成器（如 iText/Adobe 新版）常用
# 变体2: 嵌套页面树（Pages -> Pages -> Page）+ 继承属性
import zlib

def make_content(n):
    return zlib.compress(f"BT /F1 24 Tf 100 700 Td (Page {n}) Tj ET".encode())

# ---------------- 变体1: xref stream + ObjStm ----------------
def build_xrefstream():
    # 对象规划:
    # 1=Catalog, 2=Pages, 3=Font  -> 放入 ObjStm(对象6)
    # 4=Page1, 5=Content1 -> 普通对象
    # 6=ObjStm, 7=xref stream
    c1 = make_content(1)

    objs = {}
    objs[4] = (b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
               b"/Resources << /Font << /F1 3 0 R >> >> /Contents 5 0 R >>")
    objs[5] = (f"<< /Length {len(c1)} /Filter /FlateDecode >>\nstream\n".encode() + c1 + b"\nendstream")

    # ObjStm 内容: 对象1,2,3
    o1 = b"<< /Type /Catalog /Pages 2 0 R >>"
    o2 = b"<< /Type /Pages /Kids [4 0 R] /Count 1 >>"
    o3 = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"
    header = f"1 0 2 {len(o1)} 3 {len(o1)+len(o2)} "
    first = len(header)
    stm_data = header.encode() + o1 + o2 + o3
    objs[6] = (f"<< /Type /ObjStm /N 3 /First {first} /Length 0 >>".encode())  # Length 后填

    out = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    offsets = {}
    for num in (4, 5):
        offsets[num] = len(out)
        out += f"{num} 0 obj\n".encode() + objs[num] + b"\nendobj\n"
    # ObjStm 需要压缩并填 Length
    comp = zlib.compress(stm_data)
    obj6 = (f"<< /Type /ObjStm /N 3 /First {first} /Length {len(comp)} /Filter /FlateDecode >>\nstream\n".encode()
            + comp + b"\nendstream")
    offsets[6] = len(out)
    out += b"6 0 obj\n" + obj6 + b"\nendobj\n"

    # xref stream: W [1 2 1]
    # entries: 0=free, 1..3 type2(in stm6 idx0..2), 4,5,6 type1, 7 type1(self)
    entries = []
    entries += [(0, 0, 255)]                      # 0 free
    entries += [(2, 6, 0), (2, 6, 1), (2, 6, 2)]  # 1,2,3 in ObjStm
    entries += [(1, offsets[4], 0), (1, offsets[5], 0), (1, offsets[6], 0)]  # 4,5,6
    xref_off = len(out)
    entries += [(1, xref_off, 0)]                 # 7 = xref stream itself
    w = [1, 2, 1]
    raw = bytearray()
    for t, f2, f3 in entries:
        raw += bytes([t]) + f2.to_bytes(2, 'big') + bytes([f3])
    comp_x = zlib.compress(bytes(raw))
    xref_obj = (f"7 0 obj\n<< /Type /XRef /Size 8 /W [1 2 1] /Root 1 0 R "
                f"/Length {len(comp_x)} /Filter /FlateDecode >>\nstream\n").encode() + comp_x + b"\nendstream\nendobj\n"
    out += xref_obj
    out += b"startxref\n" + str(xref_off).encode() + b"\n%%EOF\n"
    open("tests/real/xrefstream.pdf", "wb").write(bytes(out))
    print("xrefstream.pdf", len(out))

# ---------------- 变体2: 嵌套页面树 + 继承属性 ----------------
def build_nested():
    c1, c2 = make_content(1), make_content(2)
    out = bytearray(b"%PDF-1.4\n")
    offsets = {}
    def add(num, data):
        offsets[num] = len(out)
        out.extend(f"{num} 0 obj\n".encode() + data + b"\nendobj\n")
    # 1 Catalog -> 2 Pages(root) -> 3 Pages(child) -> 4,5 Page
    add(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    add(2, b"<< /Type /Pages /Kids [3 0 R] /Count 2 >>")
    # 子树不带 /Type（容错场景），MediaBox/Resources 在此继承
    add(3, b"<< /Kids [4 0 R 5 0 R] /Count 2 /MediaBox [0 0 612 792] "
           b"/Resources << /Font << /F1 6 0 R >> >> >>")
    add(4, b"<< /Type /Page /Parent 3 0 R /Contents 7 0 R >>")
    add(5, b"<< /Type /Page /Parent 3 0 R /Contents 8 0 R >>")
    add(6, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    add(7, f"<< /Length {len(c1)} /Filter /FlateDecode >>\nstream\n".encode() + c1 + b"\nendstream")
    add(8, f"<< /Length {len(c2)} /Filter /FlateDecode >>\nstream\n".encode() + c2 + b"\nendstream")
    xref_off = len(out)
    out += b"xref\n0 9\n0000000000 65535 f \n"
    for i in range(1, 9):
        out += f"{offsets[i]:010d} 00000 n \n".encode()
    out += b"trailer\n<< /Size 9 /Root 1 0 R >>\nstartxref\n" + str(xref_off).encode() + b"\n%%EOF\n"
    open("tests/real/nested.pdf", "wb").write(bytes(out))
    print("nested.pdf", len(out))

build_xrefstream()
build_nested()
