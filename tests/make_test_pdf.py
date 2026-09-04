# 生成一个 10 页的测试 PDF（每页显示页码），用于验证提取工具
# 使用压缩流（FlateDecode）以测试解码路径
import zlib

def make_page(n):
    content = f"BT /F1 48 Tf 200 400 Td (Page {n}) Tj ET".encode()
    return zlib.compress(content)

pages_content = []
objects = []  # (objnum, bytes)

# 对象编号规划：
# 1=Catalog, 2=Pages, 3=Font, 4..13=Page(1..10), 14..23=Contents(1..10)
page_obj_nums = list(range(4, 14))
content_obj_nums = list(range(14, 24))

out = b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n"
offsets = {}

def add_obj(num, data):
    global out
    offsets[num] = len(out)
    out += f"{num} 0 obj\n".encode() + data + b"\nendobj\n"

# Catalog
add_obj(1, b"<< /Type /Catalog /Pages 2 0 R >>")
# Pages
kids = " ".join(f"{n} 0 R" for n in page_obj_nums)
add_obj(2, f"<< /Type /Pages /Kids [{kids}] /Count 10 >>".encode())
# Font
add_obj(3, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
# Pages + contents
for i in range(10):
    pn = page_obj_nums[i]
    cn = content_obj_nums[i]
    page_dict = (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] "
                 f"/Resources << /Font << /F1 3 0 R >> >> /Contents {cn} 0 R >>").encode()
    add_obj(pn, page_dict)
    stream = make_page(i + 1)
    add_obj(cn, f"<< /Length {len(stream)} /Filter /FlateDecode >>\nstream\n".encode() + stream + b"\nendstream")

xref_pos = len(out)
out += b"xref\n"
out += f"0 {len(offsets)+1}\n".encode()
out += b"0000000000 65535 f \n"
for i in range(1, len(offsets) + 1):
    out += f"{offsets[i]:010d} 00000 n \n".encode()
out += b"trailer\n"
out += f"<< /Size {len(offsets)+1} /Root 1 0 R >>\n".encode()
out += b"startxref\n"
out += f"{xref_pos}\n".encode()
out += b"%%EOF\n"

with open("test_10pages.pdf", "wb") as f:
    f.write(out)
print("test_10pages.pdf written,", len(out), "bytes")
