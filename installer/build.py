# Builds dist/NOVA-Installer.html: one offline page with esptool-js and both firmware builds.
# Usage: python3 installer/build.py [version]   (after tools/build-firmware.sh)
import base64, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
version = sys.argv[1] if len(sys.argv) > 1 else re.search(r'#define NOVA_VERSION\s+"([^"]+)"', open(os.path.join(ROOT, 'firmware', 'nova', 'Nova.h')).read()).group(1)
out = os.path.join(ROOT, 'dist', 'NOVA-Installer.html')
b64 = lambda p: base64.b64encode(open(p, 'rb').read()).decode()
src = open(os.path.join(HERE, 'installer.src.html')).read()
fw = {}
for v in ('ospi', 'qspi'):
    d = os.path.join(ROOT, 'build', v)
    app = open(os.path.join(d, 'nova.ino.bin'), 'rb').read()
    tag = re.search(rb'NOVA-IMAGE (\S+) (\S+) END', app)
    assert tag and tag.group(1).decode() == version and tag.group(2).decode() == v, f'build/{v} is not NOVA {version} {v}'
    fw[v] = {'bootloader': b64(os.path.join(d, 'nova.ino.bootloader.bin')), 'partitions': b64(os.path.join(d, 'nova.ino.partitions.bin')),
             'boot_app0': b64(os.path.join(HERE, 'boot_app0.bin')), 'app': b64(os.path.join(d, 'nova.ino.bin'))}
brand = os.path.join(ROOT, 'docs', 'images', 'brand')
mark = open(os.path.join(brand, 'nova-icon.svg')).read().replace('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">', '<svg viewBox="0 0 512 512" aria-hidden="true">')
src = (src.replace('__ESPTOOL_B64__', b64(os.path.join(ROOT, 'editor', 'vendor', 'esptool-js', 'bundle.js')))
          .replace('__FW_JSON__', json.dumps(fw))
          .replace('__LOGO_SVG__', mark)
          .replace('__FAVICON__', b64(os.path.join(brand, 'nova-icon-64.png')))
          .replace('__VERSION__', version))
assert '__' not in re.sub(r'<script type="text/plain".*?</script>|<script type="application/json".*?</script>', '', src, flags=re.S).replace('__proto__', '')
os.makedirs(os.path.dirname(out), exist_ok=True)
open(out, 'w').write(src)
print(out, len(src) // 1024, 'KB')
