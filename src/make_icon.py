# 生成简单的应用图标（32x32 + 16x16，蓝色文档图案），无第三方依赖
import struct

def make_image(size):
    # 32bpp BGRA 像素，自底向上
    px = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            # 归一化坐标
            nx, ny = x / size, y / size
            # 文档形状：圆角矩形区域
            m = 0.14
            in_doc = m <= nx <= 1 - m and 0.08 <= ny <= 0.92
            # 折角
            fx, fy = nx - (1 - m), ny - 0.08
            corner = (fx * fx + fy * fy) < (0.16 * 0.16) and nx > 1 - m - 0.16 and ny < 0.08 + 0.16
            if in_doc and not corner:
                # 蓝色文档
                b, g, r, a = 230, 130, 40, 255
                # 三条白色横线
                for ly in (0.32, 0.48, 0.64):
                    if abs(ny - ly) < 0.035 and 0.22 <= nx <= 0.78:
                        b, g, r = 255, 255, 255
                px += bytes([b, g, r, a])
            else:
                px += bytes([0, 0, 0, 0])
    return bytes(px)

def make_and_mask(size):
    # 1bpp AND mask，全 0（用 alpha 通道）
    row_bytes = ((size + 31) // 32) * 4
    return bytes(row_bytes * size)

def bmp_header(size):
    # BITMAPINFOHEADER，高度为 2*size（XOR + AND）
    return struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)

sizes = [32, 16]
images = []
for s in sizes:
    data = bmp_header(s) + make_image(s) + make_and_mask(s)
    images.append(data)

with open('app.ico', 'wb') as f:
    f.write(struct.pack('<HHH', 0, 1, len(sizes)))
    offset = 6 + 16 * len(sizes)
    for s, data in zip(sizes, images):
        w = s if s < 256 else 0
        h = s if s < 256 else 0
        f.write(struct.pack('<BBBBHHII', w, h, 0, 0, 1, 32, len(data), offset))
        offset += len(data)
    for data in images:
        f.write(data)
print('app.ico written')
