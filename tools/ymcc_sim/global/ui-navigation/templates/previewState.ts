import { reactive } from 'vue';
import {isRadialTestingEnabled} from '@/radial/testingState';
const params=new URLSearchParams(location.search);
export const previewRoute=reactive({path:params.get('route')||'/schedule',query:{} as Record<string,string|string[]>});
export function navigate(to:any){const requested=typeof to==='string'?to:to.path;const parsed=new URL(requested,location.origin);previewRoute.path=parsed.pathname.startsWith('/radial')&&!isRadialTestingEnabled()?'/schedule':parsed.pathname;previewRoute.query=typeof to==='object'&&to.query?{...to.query}:Object.fromEntries(parsed.searchParams);window.dispatchEvent(new CustomEvent('preview-route',{detail:previewRoute.path}));parent.postMessage({type:'preview-route',route:previewRoute.path},location.origin);}
