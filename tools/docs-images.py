# Makes the pictures in docs/images/screens from the simulator's screenshots.
# Usage: (cd tests/simulator && ./nsim 5 clean && ./nsim splash) && python3 tools/docs-images.py tests/simulator/out
import glob, os, sys
from PIL import Image, ImageDraw, ImageFilter
O = sys.argv[1].rstrip('/') + '/'
D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'docs', 'images', 'screens') + '/'
os.makedirs(D, exist_ok=True)
def scr(n, scale=2):
    return Image.open(O + n + '.ppm').convert('RGB').resize((320 * scale, 170 * scale), Image.LANCZOS)
pick = {'stations': '04_stations', 'stations-rds': '05_stations_fm_rds', 'stations-shortwave': '07_stations_sw', 'menu': '09_menu', 'tune-am': '10_tune_am',
 'tune-controls': '11_tune_chips', 'bands': '13_band_picker', 'keypad': '15_keypad', 'scope': '16_scope', 'tune-ssb': '17_tune_usb', 'tune-fm': '18_tune_fm',
 'settings': '19_settings', 'settings-scanner': '57_settings_scanner', 'search': '53_search_menu', 'scanning': '02_scan_sw', 'power': '23_power', 'apps': '31_apps',
 'ghost-box': '32_ghost_fm', 'ghost-controls': '35_ghost_speed', 'ghost-blind': '37e_ghost_blind', 'ghost-clusters': '80_ghost_clusters', 'ghost-trigger': '83_ghost_armed',
 'ghost-profiles': '85_ghost_profiles', 'ghost-catch': '87_ghost_catch', 'ghost-window': '82_ghost_window', 'noise-hunter': '42_noise_closer', 'storm-detector': '46_storm_active',
 'visualizer-meters': '60_visual_0', 'visualizer-led': '60_visual_1', 'visualizer-vu': '60_visual_2', 'visualizer-trace': '60_visual_3', 'visualizer-waterfall': '67_visual_waterfall_sw',
 'visualizer-big': '60_visual_5', 'station-names-wifi': '59b_settings_names_wifi_set', 'splash': '00_splash',
 'filter': '26_new_filter', 'volume': '08_volume', 'tune-band': '14_tune_after_band', 'search-speed': '54_search_speed', 'scan-fm': '01_scan_fm',
 'scan-offband': '03_scan_offband', 'confirm': '24_confirm', 'ghost-mark': '33_ghost_mark', 'ghost-menu': '34_ghost_controls', 'ghost-am': '36_ghost_am',
 'ghost-paused': '37_ghost_paused', 'ghost-content': '37b_ghost_content_chip', 'ghost-range': '37c_ghost_range', 'ghost-hold': '81_ghost_hold',
 'ghost-open': '84_ghost_open', 'ghost-react': '86_ghost_react_chip', 'noise-start': '41_noise_start', 'storm-quiet': '45_storm_quiet',
 'app-menu': '47_storm_controls', 'settings-scanner-bands': '58_settings_scanner_parts', 'station-names': '59_settings_names_wifi',
 'menu-tune': '52_menu_search', 'apps-menu': '31_apps'}
for k, v in pick.items(): scr(v).save(D + k + '.png', optimize=True)
if os.path.exists(O + 'splash_000.ppm'):
    frames = [Image.open(O + 'splash_%03d.ppm' % i).convert('RGB').resize((640, 340), Image.LANCZOS) for i in range(61)]
    frames = [f.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE) for f in frames]
    frames[0].save(D + 'boot-animation.gif', save_all=True, append_images=frames[1:] + [frames[-1]] * 25, duration=40, loop=0, optimize=True)
def framed(im, r=22):
    w, h = im.size; pad = 14
    base = Image.new('RGBA', (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    sh = Image.new('RGBA', base.size, (0, 0, 0, 0)); ImageDraw.Draw(sh).rounded_rectangle((pad, pad + 6, w + pad, h + pad + 6), r + 4, fill=(0, 0, 0, 150))
    base.alpha_composite(sh.filter(ImageFilter.GaussianBlur(10)))
    bez = Image.new('RGBA', (w + 12, h + 12), (0, 0, 0, 0)); ImageDraw.Draw(bez).rounded_rectangle((0, 0, w + 11, h + 11), r + 6, fill=(28, 32, 44, 255), outline=(60, 66, 90, 255), width=2)
    base.alpha_composite(bez, (pad - 6, pad - 6))
    m = Image.new('L', im.size, 0); ImageDraw.Draw(m).rounded_rectangle((0, 0, w - 1, h - 1), r, fill=255)
    base.paste(im, (pad, pad), m)
    return base
def collage(names, cols, out, scale=2, gap=26, margin=50):
    ims = [framed(scr(n, scale)) for n in names]
    w, h = ims[0].size; rows = (len(ims) + cols - 1) // cols
    W = margin * 2 + cols * w + (cols - 1) * gap; H = margin * 2 + rows * h + (rows - 1) * gap
    C = Image.new('RGBA', (W, H)); d = ImageDraw.Draw(C)
    for y in range(H):
        k = y / H; d.line([(0, y), (W, y)], fill=(int(22 - 15 * k), int(29 - 20 * k), int(58 - 40 * k), 255))
    for i, im in enumerate(ims): C.alpha_composite(im, (margin + (i % cols) * (w + gap), margin + (i // cols) * (h + gap)))
    C.convert('RGB').save(out, optimize=True)
collage(['04_stations', '10_tune_am', '32_ghost_fm', '60_visual_0', '31_apps', '46_storm_active'], 3, D + 'hero.png')
collage(['32_ghost_fm', '35_ghost_speed', '80_ghost_clusters', '83_ghost_armed', '85_ghost_profiles', '37e_ghost_blind'], 3, D + 'ghost-box-collage.png')
collage(['60_visual_0', '60_visual_1', '60_visual_2', '60_visual_3', '67_visual_waterfall_sw', '60_visual_5'], 3, D + 'visualizers-collage.png')
collage(sorted(os.path.basename(f)[:-4] for f in glob.glob(O + '70_themes_*.ppm')), 4, D + 'themes-collage.png', scale=1, gap=16, margin=36)
print('pictures in', D)
