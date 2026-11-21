#!/usr/bin/env python3
import os
import subprocess
import sys
from pathlib import Path
from PIL import Image, ImageDraw

def get_xdg_dir(type_name, default_path):
    try:
        path = subprocess.check_output(["xdg-user-dir", type_name]).decode("utf-8").strip()
        if path:
            return path
    except:
        pass
    return os.path.expanduser(default_path)

# Directories
WALLPAPER_DIR = os.path.join(get_xdg_dir("PICTURES", "~/Pictures"), "Wallpapers")

def get_anim_dir():
    videos_base = get_xdg_dir("VIDEOS", "~/Videos")
    for name in ["Animated", "Animations", "animation", "animations"]:
        candidate = os.path.join(videos_base, name)
        if os.path.isdir(candidate):
            return candidate
    default_dir = os.path.join(videos_base, "Animated")
    os.makedirs(default_dir, exist_ok=True)
    return default_dir

ANIMATION_DIR = get_anim_dir()

WALL_THUMB_DIR = os.path.expanduser("~/.cache/wallpaper_thumbs")
ANIM_THUMB_DIR = os.path.expanduser("~/.cache/animation_thumbs")

# Thumbnail target dimensions
THUMB_WIDTH = 320
THUMB_HEIGHT = 180

# Updated corner radius to match your QML UI (scale-adjusted)
CORNER_RADIUS = 32

# Ensure cache dirs exist
os.makedirs(WALL_THUMB_DIR, exist_ok=True)
os.makedirs(ANIM_THUMB_DIR, exist_ok=True)

def process_image(img_path, thumb_path):
    """Crops, resizes, and applies smooth anti-aliased rounded corners to images."""
    try:
        with Image.open(img_path) as im:
            im = im.convert("RGBA")
            w, h = im.size
            target_aspect = THUMB_WIDTH / THUMB_HEIGHT
            src_aspect = w / h

            if src_aspect > target_aspect:
                new_w = int(h * target_aspect)
                left = (w - new_w) // 2
                im = im.crop((left, 0, left + new_w, h))
            else:
                new_h = int(w / target_aspect)
                top = (h - new_h) // 2
                im = im.crop((0, top, w, top + new_h))

            im = im.resize((THUMB_WIDTH, THUMB_HEIGHT), Image.LANCZOS)

            # --- ANTI-ALIASED MASK GENERATION ---
            # 1. Scale up mask by 4x for high-precision vector drawing
            scale = 4
            big_w, big_h = THUMB_WIDTH * scale, THUMB_HEIGHT * scale
            big_radius = CORNER_RADIUS * scale

            mask_big = Image.new("L", (big_w, big_h), 0)
            draw = ImageDraw.Draw(mask_big)

            # 2. Correct bounding box end coordinates (-1 on right & bottom)
            draw.rounded_rectangle(
                [(0, 0), (big_w - 1, big_h - 1)],
                radius=big_radius,
                fill=255
            )

            # 3. Downsample back to thumb size for smooth alpha edges
            mask = mask_big.resize((THUMB_WIDTH, THUMB_HEIGHT), Image.BILINEAR)

            # Apply the smooth mask as alpha channel
            im.putalpha(mask)

            im.save(thumb_path, format="PNG")
            print(f"✓ Image Thumb: {os.path.basename(thumb_path)}")
            sys.stdout.flush()
    except Exception as e:
        print(f"✗ Image Error: {img_path} → {e}")
        sys.stdout.flush()

def process_video(vid_path, thumb_path):
    """Extracts a frame from video using ffmpeg, then crops/resizes it."""
    temp_frame = thumb_path + ".tmp.png"
    try:
        subprocess.run([
            "ffmpeg", "-y", "-ss", "00:00:01", "-i", vid_path, 
            "-vframes", "1", "-q:v", "2", temp_frame
        ], check=True, capture_output=True)
        
        process_image(temp_frame, thumb_path)
        os.remove(temp_frame)
        print(f"✓ Video Thumb: {os.path.basename(thumb_path)}")
        sys.stdout.flush()
    except subprocess.CalledProcessError as e:
        print(f"✗ Video Error (ffmpeg): {vid_path} → {e}")
        sys.stdout.flush()
    except Exception as e:
        print(f"✗ Video Error: {vid_path} → {e}")
        sys.stdout.flush()

def main():
    # Process Wallpapers
    if os.path.exists(WALLPAPER_DIR):
        for file in os.listdir(WALLPAPER_DIR):
            if file.lower().endswith((".jpg", ".jpeg", ".png", ".webp")):
                src = os.path.join(WALLPAPER_DIR, file)
                name_without_ext = os.path.splitext(file)[0]
                dst = os.path.join(WALL_THUMB_DIR, name_without_ext + ".png")
                if not os.path.exists(dst):
                    process_image(src, dst)

    # Process Animations
    if os.path.exists(ANIMATION_DIR):
        for file in os.listdir(ANIMATION_DIR):
            if file.lower().endswith((".mp4", ".mkv", ".webm", ".mov")):
                src = os.path.join(ANIMATION_DIR, file)
                name_without_ext = os.path.splitext(file)[0]
                dst = os.path.join(ANIM_THUMB_DIR, name_without_ext + ".png")
                if not os.path.exists(dst):
                    process_video(src, dst)

if __name__ == "__main__":
    main()