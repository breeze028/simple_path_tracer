import numpy as np

def read_ppm(filename, normalize=False):
    """读取PPM文件，可选是否归一化到[0, 1]"""
    with open(filename, 'rb') as f:
        raw = f.read()
        
        # 检查BOM并解码
        if raw.startswith(b'\xEF\xBB\xBF'):     # UTF-8 BOM
            content = raw[3:].decode('utf-8')
        elif raw.startswith(b'\xFF\xFE'):       # UTF-16 LE BOM
            content = raw[2:].decode('utf-16-le')
        else:                                   # 无BOM
            content = raw.decode('utf-8')
        
        lines = [line.strip() for line in content.splitlines() if line.strip()]
        
        # 解析魔术数字
        magic_number = lines[0]
        if magic_number != 'P3':
            raise ValueError(f"不是P3格式的PPM文件，实际是: {magic_number}")
        
        # 跳过注释
        idx = 1
        while idx < len(lines) and lines[idx].startswith('#'):
            idx += 1
        
        # 读取宽度、高度、最大颜色值
        width, height = map(int, lines[idx].split())
        idx += 1
        max_color = int(lines[idx])
        idx += 1
        
        # 读取像素数据
        data = []
        for line in lines[idx:]:
            data.extend(line.split())
        
        # 转换为NumPy数组，并根据normalize参数决定是否归一化
        pixels = np.array(data, dtype=np.float32 if normalize else np.uint8)
        pixels = pixels.reshape(height, width, 3)
        
        if normalize:
            pixels /= max_color  # 归一化到[0, 1]
        
        return pixels

def calculate_mse(img1, img2, normalized=True):
    """计算MSE（默认使用归一化后的值）"""
    if img1.shape != img2.shape:
        raise ValueError("两幅图像尺寸不匹配")
    
    if not normalized:
        # 如果输入是0-255的整数，转换为float再计算
        img1 = img1.astype(np.float32)
        img2 = img2.astype(np.float32)
    
    mse = np.mean((img1 - img2) ** 2)
    return mse

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("用法: python ppm_mse.py <file1.ppm> <file2.ppm>")
        sys.exit(1)
    
    try:
        # 读取时直接归一化到[0, 1]
        img1 = read_ppm(sys.argv[1], normalize=True)
        img2 = read_ppm(sys.argv[2], normalize=True)
        
        mse_normalized = calculate_mse(img1, img2, normalized=True)
        print(f"MSE (0-1范围): {mse_normalized:.6f}")
        
        # 如果需要0-255范围的MSE，可以这样计算
        # img1_255 = (img1 * 255).astype(np.uint8)
        # img2_255 = (img2 * 255).astype(np.uint8)
        # mse_255 = calculate_mse(img1_255, img2_255, normalized=False)
        # print(f"MSE (0-255范围): {mse_255:.1f}")
    except Exception as e:
        print(f"错误: {e}")
        sys.exit(1)