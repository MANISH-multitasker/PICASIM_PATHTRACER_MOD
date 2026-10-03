#!/usr/bin/env python3
"""Generate Windows, macOS, Android and iOS icons from source images.

Source images (in AndroidIcon/):
  ic_launcher_foreground.png - plane on transparent background (432x432)
  ic_launcher_background.png - blue gradient background (432x432)

Generates:
  Android: adaptive icon layers at all mipmap densities
  Windows: composited multi-size ICO at resources/PicaSim.ico
  macOS:   composited .icns at resources/PicaSim.icns
  iOS:     composited PNG icons in ios/Assets.xcassets/AppIcon.appiconset/
"""

import os
from PIL import Image, ImageDraw

script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
source_dir = os.path.join(script_dir, "AndroidIcon")

fg = Image.open(os.path.join(source_dir, "ic_launcher_foreground.png"))
bg = Image.open(os.path.join(source_dir, "ic_launcher_background.png"))

# --- Android adaptive icon layers ---
print("Android:")
ANDROID_SIZES = {
    "mipmap-mdpi": 108,
    "mipmap-hdpi": 162,
    "mipmap-xhdpi": 216,
    "mipmap-xxhdpi": 324,
    "mipmap-xxxhdpi": 432,
}
res_dir = os.path.join(project_dir, "android", "app", "src", "main", "res")

for name, src in (("ic_launcher_foreground", fg), ("ic_launcher_background", bg)):
    for folder, size in ANDROID_SIZES.items():
        out_dir = os.path.join(res_dir, folder)
        os.makedirs(out_dir, exist_ok=True)
        resized = src.resize((size, size), Image.LANCZOS)
        resized.save(os.path.join(out_dir, f"{name}.png"))
        print(f"  {folder}/{name}.png ({size}x{size})")

# --- Windows ICO (composited with rounded corners) ---
print("Windows:")
ICO_SIZES = [256, 128, 64, 48, 32, 16]


def make_rounded_mask(size, radius):
    """Create a rounded rectangle alpha mask."""
    mask = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return mask


ico_images = []
for size in ICO_SIZES:
    # Composite foreground onto background
    bg_resized = bg.resize((size, size), Image.LANCZOS).convert("RGBA")
    fg_resized = fg.resize((size, size), Image.LANCZOS).convert("RGBA")
    composited = Image.alpha_composite(bg_resized, fg_resized)
    # Apply rounded corners
    radius = max(size // 8, 2)
    mask = make_rounded_mask(size, radius)
    composited.putalpha(mask)
    ico_images.append(composited)
    print(f"  {size}x{size}")

ico_path = os.path.join(script_dir, "PicaSim.ico")
ico_images[0].save(ico_path, format="ICO", sizes=[(s, s) for s in ICO_SIZES],
                    append_images=ico_images[1:])
print(f"  -> {ico_path}")

# --- macOS .icns (composited, no rounded corners - macOS applies them automatically) ---
print("macOS:")
import subprocess
import tempfile
import shutil

ICNS_SIZES = {
    "icon_16x16.png": 16,
    "icon_16x16@2x.png": 32,
    "icon_32x32.png": 32,
    "icon_32x32@2x.png": 64,
    "icon_128x128.png": 128,
    "icon_128x128@2x.png": 256,
    "icon_256x256.png": 256,
    "icon_256x256@2x.png": 512,
    "icon_512x512.png": 512,
    "icon_512x512@2x.png": 1024,
}

iconset_dir = os.path.join(tempfile.mkdtemp(), "PicaSim.iconset")
os.makedirs(iconset_dir)

for filename, size in ICNS_SIZES.items():
    bg_resized = bg.resize((size, size), Image.LANCZOS).convert("RGBA")
    fg_resized = fg.resize((size, size), Image.LANCZOS).convert("RGBA")
    composited = Image.alpha_composite(bg_resized, fg_resized)
    # Convert to RGB (no alpha) - macOS applies its own mask
    composited_rgb = Image.new("RGB", composited.size, (255, 255, 255))
    composited_rgb.paste(composited, mask=composited.split()[3])
    composited_rgb.save(os.path.join(iconset_dir, filename), format="PNG")
    print(f"  {filename} ({size}x{size})")

icns_path = os.path.join(script_dir, "PicaSim.icns")
subprocess.run(["iconutil", "-c", "icns", iconset_dir, "-o", icns_path], check=True)
shutil.rmtree(os.path.dirname(iconset_dir))
print(f"  -> {icns_path}")

# --- iOS App Icon (composited, no rounded corners - iOS applies them automatically) ---
print("iOS:")
import json

# All required iOS icon sizes: (width_points, scale) -> pixel size
IOS_ICON_ENTRIES = [
    # iPhone
    {"size": "20x20",   "idiom": "iphone", "scale": "2x"},  # 40px
    {"size": "20x20",   "idiom": "iphone", "scale": "3x"},  # 60px
    {"size": "29x29",   "idiom": "iphone", "scale": "2x"},  # 58px
    {"size": "29x29",   "idiom": "iphone", "scale": "3x"},  # 87px
    {"size": "40x40",   "idiom": "iphone", "scale": "2x"},  # 80px
    {"size": "40x40",   "idiom": "iphone", "scale": "3x"},  # 120px
    {"size": "60x60",   "idiom": "iphone", "scale": "2x"},  # 120px
    {"size": "60x60",   "idiom": "iphone", "scale": "3x"},  # 180px
    # iPad
    {"size": "20x20",   "idiom": "ipad",   "scale": "1x"},  # 20px
    {"size": "20x20",   "idiom": "ipad",   "scale": "2x"},  # 40px
    {"size": "29x29",   "idiom": "ipad",   "scale": "1x"},  # 29px
    {"size": "29x29",   "idiom": "ipad",   "scale": "2x"},  # 58px
    {"size": "40x40",   "idiom": "ipad",   "scale": "1x"},  # 40px
    {"size": "40x40",   "idiom": "ipad",   "scale": "2x"},  # 80px
    {"size": "76x76",   "idiom": "ipad",   "scale": "1x"},  # 76px
    {"size": "76x76",   "idiom": "ipad",   "scale": "2x"},  # 152px
    {"size": "83.5x83.5", "idiom": "ipad", "scale": "2x"},  # 167px
    # App Store
    {"size": "1024x1024", "idiom": "ios-marketing", "scale": "1x"},  # 1024px
]

ios_iconset_dir = os.path.join(project_dir, "ios", "Assets.xcassets", "AppIcon.appiconset")
os.makedirs(ios_iconset_dir, exist_ok=True)

# Generate icons and build Contents.json
contents_images = []
generated_files = set()

for entry in IOS_ICON_ENTRIES:
    size_str = entry["size"]
    scale_str = entry["scale"]
    idiom = entry["idiom"]

    # Calculate pixel size
    points = float(size_str.split("x")[0])
    scale = int(scale_str[0])
    pixel_size = int(points * scale)

    filename = f"icon_{pixel_size}x{pixel_size}.png"

    # Only generate the file once per unique pixel size
    if filename not in generated_files:
        bg_resized = bg.resize((pixel_size, pixel_size), Image.LANCZOS).convert("RGBA")
        fg_resized = fg.resize((pixel_size, pixel_size), Image.LANCZOS).convert("RGBA")
        composited = Image.alpha_composite(bg_resized, fg_resized)
        # Convert to RGB (no alpha) - iOS app icons must be opaque
        composited_rgb = Image.new("RGB", composited.size, (255, 255, 255))
        composited_rgb.paste(composited, mask=composited.split()[3])
        composited_rgb.save(os.path.join(ios_iconset_dir, filename), format="PNG")
        generated_files.add(filename)
        print(f"  {filename} ({pixel_size}x{pixel_size})")

    contents_images.append({
        "size": size_str,
        "idiom": idiom,
        "filename": filename,
        "scale": scale_str,
    })

contents_json = {
    "images": contents_images,
    "info": {
        "version": 1,
        "author": "generate_icons.py",
    },
}

contents_path = os.path.join(ios_iconset_dir, "Contents.json")
with open(contents_path, "w") as f:
    json.dump(contents_json, f, indent=2)
    f.write("\n")
print(f"  -> {contents_path}")

print("Done")
