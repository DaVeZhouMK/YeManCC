import { reactive } from 'vue';
import {isRadialTestingEnabled} from '@/radial/testingState';
const params=new URLSearchParams(location.search);
export const previewRoute=reactive({path:params.get('route')||'/schedule'});
export function navigate(to:any){const requested=typeof to==='string'?to:to.path;previewRoute.path=requested.startsWith('/radial')&&!isRadialTestingEnabled()?'/schedule':requested;window.dispatchEvent(new CustomEvent('preview-route',{detail:previewRoute.path}));parent.postMessage({type:'preview-route',route:previewRoute.path},location.origin);}
