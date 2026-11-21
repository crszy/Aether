# Builds dotfiles/caelestia-cli-win: the upstream caelestia CLI + small Windows patches. Upstream clone untouched.
#   py -I make-cli-win.py
import io, os, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'caelestia-cli', 'src')
DST = os.path.join(HERE, 'caelestia-cli-win', 'src')

def rd(p): return io.open(p, encoding='utf-8').read()
def wr(p, s): io.open(p, 'w', encoding='utf-8', newline='\n').write(s)
def rep(rel, a, b, count=1):
    p = os.path.join(DST, 'caelestia', rel); s = rd(p)
    assert s.count(a) == count, (rel, a[:70], s.count(a))
    wr(p, s.replace(a, b))

if os.path.isdir(DST): shutil.rmtree(DST)
shutil.copytree(SRC, DST, ignore=shutil.ignore_patterns('__pycache__'))

# 1. theme export writes Hyprland/terminal/Discord/GTK files and uses fcntl (Linux only): off on Windows
rep('utils/theme.py', 'import fcntl\n', 'import sys\ntry:\n    import fcntl\nexcept ImportError:  # Windows\n    fcntl = None\n')
rep('utils/theme.py', 'def apply_colours(colours: dict[str, str], mode: str) -> None:\n',
    'def apply_colours(colours: dict[str, str], mode: str) -> None:\n'
    '    if sys.platform == "win32":  # Windows: the shell reads scheme.json itself; no Linux app themes to write\n'
    '        return\n')

# 2. symlinks need special rights on Windows: copy the file instead
rep('utils/wallpaper.py', '    wallpaper_link_path.symlink_to(wall)\n',
    '    try:\n        wallpaper_link_path.symlink_to(wall)\n    except OSError:  # Windows without symlink rights\n'
    '        import shutil as _sh; _sh.copyfile(wall, wallpaper_link_path)\n')
rep('utils/wallpaper.py', '    wallpaper_thumbnail_path.symlink_to(thumb)\n',
    '    try:\n        wallpaper_thumbnail_path.symlink_to(thumb)\n    except OSError:  # Windows without symlink rights\n'
    '        import shutil as _sh; _sh.copyfile(thumb, wallpaper_thumbnail_path)\n')

# 3. random wallpaper: monitor sizes from Windows instead of hyprctl
rep('utils/wallpaper.py', '    monitors = cast(list[dict[str, int]], message("monitors"))\n',
    '    if os.name == "nt":\n'
    '        import ctypes\n'
    '        u = ctypes.windll.user32\n'
    '        monitors = [{"width": u.GetSystemMetrics(0), "height": u.GetSystemMetrics(1)}]\n'
    '    else:\n'
    '        monitors = cast(list[dict[str, int]], message("monitors"))\n')

# 4. notifications: notify-send does not exist on Windows
rep('utils/notify.py', 'def notify(*args: str) -> str:\n',
    'def notify(*args: str) -> str:\n'
    '    import shutil as _sh\n'
    '    if not _sh.which("notify-send"):\n'
    '        return ""\n')

# 5. atomic_write: Windows can refuse os.replace while the shell has the file open for reading; retry briefly
rep('utils/paths.py', '        os.replace(f.name, path)\n',
    '        for _try in range(20):\n'
    '            try:\n'
    '                os.replace(f.name, path)\n'
    '                break\n'
    '            except PermissionError:\n'
    '                if _try == 19: raise\n'
    '                import time; time.sleep(0.05)\n')
print('caelestia-cli-win ready')
