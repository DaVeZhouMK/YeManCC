import win32gui, win32ui, win32con
import PIL.Image

exe = r'C:\SOFT\YeMan\YeManCC4\YeManCC3\native\YeManCC.exe'
hicons, _ = win32gui.ExtractIconEx(exe, 0)
print('icon groups extracted:', len(hicons))
hicon = hicons[0]

size = 64
hdc_screen = win32gui.GetDC(0)
hdc = win32ui.CreateDCFromHandle(hdc_screen)
hbmp = win32ui.CreateBitmap()
hbmp.CreateCompatibleBitmap(hdc, size, size)
memdc = win32ui.CreateDCFromHandle(win32gui.CreateCompatibleDC(hdc_screen))
old = memdc.SelectObject(hbmp)
memdc.BitBlt((0, 0), (size, size), hdc, (0, 0), win32con.SRCCOPY)
memdc.DrawIcon((0, 0), hicon)
memdc.SelectObject(old)

info = hbmp.GetInfo()
bits = hbmp.GetBitmapBits(True)
img = PIL.Image.frombuffer('RGBA', (info['bmWidth'], info['bmHeight']), bits, 'raw', 'BGRA', 0, 1)
out = r'C:\SOFT\YeMan\YeManCC4\YeManCC3\native\YeManCC_exe_icon_check.png'
img.save(out)
print('wrote', out, img.size)
