import { http, type HttpResponse } from './api';

export type SteamRequestOptions = {
  method?: string;
  headers?: Record<string, string>;
  body?: string;
};

export async function requestSteamHttp(
  url: string,
  options: SteamRequestOptions = {},
): Promise<HttpResponse> {
  // This shared helper is also used by dynamic backgrounds. Keep it as a
  // transparent wrapper so all Steam callers use the same native route and
  // the chart-specific headers cannot change background fetching behavior.
  const response = await http.request(url, options);
  if (response.status < 200 || response.status >= 300) {
    throw new Error(`Steam HTTP ${response.status}`);
  }
  return response;
}

export async function requestSteamJson<T>(url: string): Promise<T> {
  let response: HttpResponse;
  try {
    response = await requestSteamHttp(url, { headers: { Accept: 'application/json' } });
  } catch (error) {
    throw new Error(`Steam 网络访问失败：${(error as Error).message}`);
  }
  try {
    return JSON.parse(response.body) as T;
  } catch {
    throw new Error('Steam 返回的资料格式无法读取');
  }
}
