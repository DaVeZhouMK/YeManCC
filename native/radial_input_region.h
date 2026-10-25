#pragma once
#include <windows.h>
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>
namespace ymcc::radial {
inline HRGN makeInputRegion(const nlohmann::json& polygons,double scale){
 if(!std::isfinite(scale)||scale<.35||scale>4)throw std::runtime_error("Invalid wheel scale");
  if(!polygons.is_array()||polygons.size()>32)throw std::runtime_error("Invalid wheel region");
  HRGN joined=CreateRectRgn(0,0,0,0);if(!joined)throw std::runtime_error("Wheel region allocation failed");
  try{for(const auto& p:polygons){if(!p.is_array()||p.size()<3||p.size()>160)throw std::runtime_error("Invalid wheel polygon");std::vector<POINT> points;
    for(const auto& v:p){if(!v.is_array()||v.size()!=2||!v[0].is_number()||!v[1].is_number())throw std::runtime_error("Invalid wheel point");
      const double x=v[0].get<double>(),y=v[1].get<double>();if(!std::isfinite(x)||!std::isfinite(y)||x<0||x>496||y<0||y>560)throw std::runtime_error("Wheel point out of bounds");
      points.push_back({(LONG)std::lround(x*scale),(LONG)std::lround(y*scale)});}
    HRGN part=CreatePolygonRgn(points.data(),(int)points.size(),WINDING);if(!part)throw std::runtime_error("Wheel polygon allocation failed");CombineRgn(joined,joined,part,RGN_OR);DeleteObject(part);
   }
   // A hard native hole remains even if a compromised/buggy renderer supplies
   // a polygon spanning the centre. Empty centre receives the underlying app.
   HRGN hole=CreateEllipticRgn((int)std::floor(182*scale),(int)std::floor(182*scale),(int)std::ceil(314*scale),(int)std::ceil(314*scale));
   if(!hole)throw std::runtime_error("Wheel hole allocation failed");CombineRgn(joined,joined,hole,RGN_DIFF);DeleteObject(hole);
   return joined;
  }catch(...){if(joined)DeleteObject(joined);throw;}
}
}
