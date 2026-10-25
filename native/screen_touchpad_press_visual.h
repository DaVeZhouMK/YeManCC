#pragma once
// Included inside screenpads. Cached frames extend only the disabled visual HWND;
// input HWNDs, hit geometry, native touch sources and focus policy are unchanged.
inline unsigned long long pressFrameBuilds=0;
inline int pressGlowPadding(int scale) noexcept {return MulDiv(16,std::clamp(scale,50,200),100);}
inline DWORD scalePressPixel(DWORD pixel,unsigned alpha) noexcept {
    DWORD result=0;for(unsigned shift:{0u,8u,16u,24u})result|=((((pixel>>shift)&255)*alpha+127)/255)<<shift;return result;
}
inline bool allocatePressBitmap(OutlineBitmap& image,int width,int height) {
    image.reset();image.dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;if(image.dc)image.bitmap=CreateDIBSection(image.dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!image.dc || !image.bitmap || !bits){windowError=ERROR_NOT_ENOUGH_MEMORY;image.reset();return false;}
    const auto previous=SelectObject(image.dc,image.bitmap);
    if(!previous || previous==HGDI_ERROR){windowError=ERROR_INVALID_HANDLE;image.reset();return false;}
    image.previous=previous;image.pixels=static_cast<DWORD*>(bits);image.width=width;image.height=height;
    std::fill_n(image.pixels,(size_t)width*height,0u);return true;
}
inline DWORD pressGlowPixel(int x,int y,int width,int height,int padding) noexcept {
    // Skip the flat interior. The halo follows the same rounded border, with a
    // quadratic falloff; no blur, animation, per-MOVE rasterization or timer.
    if(x>=12 && x<width-12 && y>=12 && y<height-12)return 0;
    const double radius=std::min(10.0,std::min(width,height)/2.0-2.0);
    const double qx=std::abs(x+.5-width/2.0)-(width/2.0-1.5-radius);
    const double qy=std::abs(y+.5-height/2.0)-(height/2.0-1.5-radius);
    const double distance=std::hypot(std::max(qx,0.0),std::max(qy,0.0))+std::min(std::max(qx,qy),0.0)-radius;
    if(distance<0 || distance>=padding)return 0;
    const double fade=1-distance/padding;const DWORD a=(DWORD)std::lround(210*fade*fade);
    return (a<<24)|(a<<16)|(a<<8)|a;
}
inline bool buildPressFrame(const OutlineBitmap& artwork,OutlineBitmap& frame,int padding,bool pressed) {
    const int width=artwork.width,height=artwork.height,w=width+2*padding,h=height+2*padding;
    if(frame.dc && frame.width==w && frame.height==h)return true;
    if(!allocatePressBitmap(frame,w,h))return false;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        const int sx=x-padding,sy=y-padding;
        const DWORD base=sx>=0 && sy>=0 && sx<width && sy<height?artwork.pixels[(size_t)sy*width+sx]:0;
        const DWORD glow=pressed?pressGlowPixel(sx,sy,width,height,padding):0;
        // Premultiplied source-over. Border/glyph pixels retain their original shape.
        frame.pixels[(size_t)y*w+x]=base+scalePressPixel(glow,255-(base>>24));
    }
    ++pressFrameBuilds;return true;
}
